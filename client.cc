#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <iostream>

int main(int argc, char* argv[]) {
    int error;
    int len;
    ssize_t bytes_sent;
    ssize_t bytes_received;

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
        perror("Unable to initial connection on socket\n");
        exit(1);
    }

    char* recv_msg = new char[256];

    std::cout << "Established socket connection" << std::endl;

    // recv(2) - receive a message on the socket
    bytes_received = recv(sd,       // socket
                          recv_msg, // buffer to store received message
                          256,      // max length of buffer
                          0);       // flags
    if (bytes_received <= 0) {
        perror("Unable to receive message\n");
        exit(1);
    }

    std::cout << "Received message: " << recv_msg << std::endl;

    // send(2) - send a message on the socket
    const char* send_msg = "Hello from the client!";
    len = strlen(send_msg);

    bytes_sent = send(sd,       // socket
                      send_msg, // buffer to store message to send
                      len,      // length of message
                      0);       // flags
    if (bytes_sent == -1) {
        perror("Unable to send message\n");
        exit(1);
    }

    std::cout << "Sent message: " << send_msg << std::endl;

    // Clean up descriptors, memory
    delete[] recv_msg;
    close(sd);
    freeaddrinfo(res);

    std::cout << "Clean up complete" << std::endl;

    return 0;
}
