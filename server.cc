#include <netdb.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <iostream>
#include <set>
#include <vector>

std::vector<int> sockets;
pthread_mutex_t sockets_mutex;

void* poller(void* arg) {
    int error;
    // Used to track the descriptors which have been sent to the buffer
    std::set<int> sent;

    while (true) {
        fd_set fds;
        FD_ZERO(&fds);

        pthread_mutex_lock(&sockets_mutex);
        for (int cd : sockets) {
            FD_SET(cd, &fds);
        }
        pthread_mutex_unlock(&sockets_mutex);

        // select(2) - determine which sockets are pending
        struct timeval timeout;
        timeout.tv_sec = 1;
        error = select(FD_SETSIZE, // maximum number of descriptors
                       &fds,       // read descriptors
                       NULL,       // write descriptors
                       NULL,       // error descriptors
                       &timeout);  // timeout (NULL means no timeout)
        if (error == -1) {
            perror("[Poller] Unable to select descriptors");
            exit(1);
        }

        pthread_mutex_lock(&sockets_mutex);
        for (int cd : sockets) {
            if (FD_ISSET(cd, &fds)) {
                if (sent.find(cd) == sent.end()) {
                    // If the descriptor is pending but was not sent, send it
                    printf("[Poller] Sent client %d to be read\n", cd);
                    sent.insert(cd);
                }
            } else {
                if (sent.find(cd) != sent.end()) {
                    // If the descriptor is not pending but was sent, remove it
                    printf("[Poller] Client %d has been handled\n", cd);
                    sent.erase(cd);
                }
            }
        }
        pthread_mutex_unlock(&sockets_mutex);
    }

    return NULL;
}

int main(int argc, char* argv[]) {
    // Initialize values
    pthread_mutex_init(&sockets_mutex, NULL);

    int error;

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
        fprintf(stderr, "[Main] getaddrinfo failed: %s\n", gai_strerror(error));
        exit(1);
    }

    // socket(2) - set up socket for bidirectional communication
    int sd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sd == -1) {
        perror("[Main] Unable to create socket");
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
        perror("[Main] Failed to set socket options");
        exit(1);
    }

    printf("[Main] Created server socket %d\n", sd);

    // bind(2) - bind socket to a specific port (>1023)
    error = bind(sd, res->ai_addr, res->ai_addrlen);
    if (error == -1) {
        perror("[Main] Unable to bind socket to port");
        exit(1);
    }

    // listen(2) - listen for incoming connections on the socket
    error = listen(sd, 5);
    if (error == -1) {
        perror("[Main] Unable to listen with socket");
        exit(1);
    }

    // Poll open descriptors for pending data to read
    pthread_t poller_thread;
    pthread_create(&poller_thread, NULL, poller, NULL);

    while (true) {
        // accept(2) - accepting an incoming connection on the socket
        sockaddr_storage address;
        socklen_t address_len = sizeof(address);
        printf("[Main] Waiting for incoming socket connection\n");
        int cd = accept(sd, (sockaddr*) &address, &address_len);
        if (cd == -1) {
            perror("[Main] Unable to accept socket");
            exit(1);
        }

        printf("[Main] Accepted new client connection on socket %d\n", cd);

        pthread_mutex_lock(&sockets_mutex);
        sockets.push_back(cd);
        pthread_mutex_unlock(&sockets_mutex);
    }

    // Clean up descriptors, memory
    close(sd);
    freeaddrinfo(res);
    pthread_mutex_destroy(&sockets_mutex);

    std::cout << "[Main] Clean up complete" << std::endl;

    return 0;
}
