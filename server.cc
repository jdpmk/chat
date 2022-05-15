#include <netdb.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <iostream>
#include <vector>

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
            break;
        } else if (bytes_received < 0) {
            perror("Unable to receive message\n");
            // This thread finishes but other threads may continue
            // Clean up memory the thread owns, and exit the thread
            delete[] recv_msg;
            pthread_exit(NULL);
        }

        printf("[from %d] %s", cd, recv_msg);
        fflush(stdout);
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
        perror("Unable to create socket\n");
        exit(1);
    }

    std::cout << "Created socket: " << sd << std::endl;

    // bind(2) - bind socket to a specific port (>1023)
    error = bind(sd, res->ai_addr, res->ai_addrlen);
    if (error == -1) {
        perror("Unable to bind socket to port\n");
        exit(1);
    }

    while (true) {
        // listen(2) - listen for incoming connections on the socket
        error = listen(sd, 5);
        if (error == -1) {
            perror("Unable to listen with socket\n");
            exit(1);
        }

        std::cout << "Listening on socket: " << sd << std::endl;

        // accept(2) - accepting an incoming connection on the socket
        sockaddr_storage address;
        socklen_t address_len = sizeof(address);
        int _cd = accept(sd, (sockaddr*) &address, &address_len);
        if (_cd == -1) {
            perror("Unable to accept socket\n");
            exit(1);
        }

        std::cout << "Accepted socket: " << _cd << std::endl;

        // Heap allocate client descriptor to pass as void* to pthread
        int* cd = new int;
        *cd = _cd;

        // Process each client in a separate pthread
        pthread_t* thread = new pthread_t;
        threads.push_back(thread);
        pthread_create(thread, NULL, handle_client, (void*) cd);
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
