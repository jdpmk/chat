#include <netdb.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <iostream>
#include <vector>

// TODO: Implement a thread-safe list
std::vector<int> sockets;
pthread_mutex_t broadcast_mutex = PTHREAD_MUTEX_INITIALIZER;

void broadcast(int source_cd, const char* msg, ssize_t len) {
    char* formatted_msg;
    ssize_t formatted_len;

    asprintf(&formatted_msg, "[Client %d] %s", source_cd, msg);
    formatted_len = strlen(formatted_msg);

    ssize_t bytes_sent;

    pthread_mutex_lock(&broadcast_mutex);
    for (int cd : sockets) {
        // Avoid broadcasting the message to the client which sent it
        if (cd == source_cd) {
            continue;
        }

        bytes_sent = send(cd,            // socket
                          formatted_msg, // buffer to store message
                          formatted_len, // length of message
                          0);            // flags
        // TODO: Gracefully detect and handle unexpected server crashes
        //       Try checking bytes_sent == 0?
        if (bytes_sent == -1) {
            pthread_mutex_unlock(&broadcast_mutex);
            perror("Unable to send message");
            exit(1);
        }
    }
    pthread_mutex_unlock(&broadcast_mutex);

    free(formatted_msg);
}

void* handle_client(void* arg) {
    int* cd_ptr = (int*) arg;
    int cd = *cd_ptr;

    char* recv_msg = new char[256];
    ssize_t bytes_received;

    while (true) {
        // recv(2) - receive a message on the socket
        bytes_received = recv(cd,       // socket
                              recv_msg, // buffer to store received message
                              256,      // max length of buffer
                              0);       // flags
        if (bytes_received == 0) {
            printf("[%d] Client has closed the connection\n", cd);

            pthread_mutex_lock(&broadcast_mutex);
            sockets.erase(std::find(sockets.begin(), sockets.end(), cd));
            pthread_mutex_unlock(&broadcast_mutex);

            break;
        } else if (bytes_received < 0) {
            perror("Unable to receive message");
            // This thread finishes but other threads may continue
            // Clean up memory the thread owns, and exit the thread
            delete[] recv_msg;
            pthread_exit(NULL);
        }

        // recv does not null-terminate the message
        assert(bytes_received < 256);
        recv_msg[bytes_received] = '\0';

        broadcast(cd, recv_msg, bytes_received);
    }

    delete[] recv_msg;
    delete cd_ptr;

    return NULL;
}

int main(int argc, char* argv[]) {
    int error;
    std::vector<pthread_t*> threads;

    // getaddrinfo(3) - set up structs
    addrinfo* res;
    addrinfo hints;

    memset(&hints, 0, sizeof(hints));
    hints.ai_flags    = AI_PASSIVE;  // for use in bind(2)
    hints.ai_family   = PF_INET;     // protocol
    hints.ai_socktype = SOCK_STREAM; // sequenced connection
    hints.ai_protocol = IPPROTO_TCP; // tcp protocol

    error = getaddrinfo("127.0.0.1", // hostname
                        "3000",      // port
                        &hints,
                        &res);

    if (error != 0) {
        fprintf(stderr, "getaddrinfo failed: %s\n", gai_strerror(error));
        exit(1);
    }

    // socket(2) - set up socket for bidirectional communication
    int sd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sd == -1) {
        perror("Unable to create socket");
        exit(1);
    }

    // setsockopt(2) - set options for socket (reuse, etc.)
    int reuse = 1;
    error = setsockopt(sd,                   // socket
                       SOL_SOCKET,           // level
                       SO_REUSEADDR,         // option name
                       (const char*) &reuse, // option value
                       sizeof(reuse));
    if (error == -1) {
        perror("Failed to set socket options");
        exit(1);
    }

    std::cout << "Created socket: " << sd << std::endl;

    // bind(2) - bind socket to a specific port (>1023)
    error = bind(sd, res->ai_addr, res->ai_addrlen);
    if (error == -1) {
        perror("Unable to bind socket to port");
        exit(1);
    }

    // listen(2) - listen for incoming connections on the socket
    error = listen(sd, 5);
    if (error == -1) {
        perror("Unable to listen with socket");
        exit(1);
    }

    std::cout << "Listening on socket: " << sd << std::endl;

    while (true) {
        // accept(2) - accepting an incoming connection on the socket
        sockaddr_storage address;
        socklen_t address_len = sizeof(address);
        int cd = accept(sd, (sockaddr*) &address, &address_len);
        if (cd == -1) {
            perror("Unable to accept socket");
            exit(1);
        }

        std::cout << "Accepted socket: " << cd << std::endl;

        // Heap allocate client descriptor to pass as void* to pthread
        int* _cd = new int;
        *_cd = cd;

        pthread_mutex_lock(&broadcast_mutex);
        sockets.push_back(cd);
        pthread_mutex_unlock(&broadcast_mutex);

        // Process each client in a separate pthread
        pthread_t* thread = new pthread_t;
        threads.push_back(thread);
        pthread_create(thread, NULL, handle_client, (void*) _cd);
    }

    // Clean up descriptors, memory
    for (pthread_t* thread : threads) {
        delete thread;
    }
    close(sd);
    freeaddrinfo(res);

    std::cout << "Clean up complete" << std::endl;

    return 0;
}
