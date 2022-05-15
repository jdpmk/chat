#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <iostream>

int main(int argc, char* argv[]) {
    int error;
    ssize_t bytes_sent;

    // getaddrinfo(3) - set up structs
    addrinfo* res;
    addrinfo hints;

    memset(&hints, 0, sizeof(hints));
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

    // connect(2) - initiate a socket connection
    error = connect(sd, res->ai_addr, res->ai_addrlen);
    if (error == -1) {
        perror("Unable to initialize connection on socket\n");
        exit(1);
    }

    std::cout << "Established socket connection" << std::endl;

    char* send_msg = new char[256];
    size_t len;

    while (true) {
        // Get input from stdin
        printf(">> ");
        fflush(stdin);
        getline(&send_msg, &len, stdin);

        // send(2) - send a message on the socket
        bytes_sent = send(sd,       // socket
                          send_msg, // buffer to store message to send
                          len,      // length of message
                          0);       // flags
        // TODO: Gracefully detect and handle unexpected server crashes
        //       Try checking bytes_sent == 0?
        if (bytes_sent == -1) {
            perror("Unable to send message\n");
            exit(1);
        }

        printf("[to %d] %s", sd, send_msg);
        fflush(stdout);
    }

    // Clean up descriptors, memory
    delete[] send_msg;
    close(sd);
    freeaddrinfo(res);

    std::cout << "Clean up complete" << std::endl;

    return 0;
}
