#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <iostream>

int main(int argc, char* argv[]) {
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

    error = listen(sd, 5);
    if (error == -1) {
        perror("Unable to listen with socket\n");
        exit(1);
    }

    std::cout << "Listening on socket: " << sd << std::endl;

    // accept(2) - accepting an incoming connection on the socket
    sockaddr_storage address;
    socklen_t address_len = sizeof(address);
    int cd = accept(sd, (sockaddr*) &address, &address_len);
    if (cd == -1) {
        perror("Unable to accept socket\n");
        exit(1);
    }

    std::cout << "Accepted socket: " << cd << std::endl;

    // send(2) - send a message on the socket
    const char* msg = "Connected to server";
    int len = strlen(msg);

    ssize_t bytes_sent = send(cd, msg, len, 0);
    if (bytes_sent == -1) {
        perror("Unable to send message\n");
        exit(1);
    }

    std::cout << "Sent message: " << msg << std::endl;

    // Clean up descriptors, memory
    close(cd);
    close(sd);
    freeaddrinfo(res);

    std::cout << "Clean up complete" << std::endl;

    return 0;
}
