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
                        "80",        // port (http)
                        &hints,
                        &res);
    if (error != 0) {
        fprintf(stderr, "getaddrinfo failed: %s\n", gai_strerror(error));
        exit(1);
    }

    // socket(2) - set up socket for bidirectional communication
    int sd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sd == -1) {
        perror("Unable to listen with socket\n");
        exit(1);
    }

    // Clean up descriptors, memory
    close(sd);
    freeaddrinfo(res);

    return 0;
}
