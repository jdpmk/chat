#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <iostream>

void* receiver(void* arg) {
    int* sd_ptr = (int*) arg;
    int sd = *sd_ptr;

    char* recv_msg = new char[256];
    ssize_t bytes_received;

    while (true) {
        // recv(2) - receive a message on the socket
        bytes_received = recv(sd,       // socket
                              recv_msg, // buffer to store received message
                              256,      // max length of buffer
                              0);       // flags
        if (bytes_received == 0) {
            printf("[Server %d] Server has closed the connection\n", sd);
            break;
        } else if (bytes_received < 0) {
            perror("Unable to receive message");
            exit(1);
        }

        // recv does not null-terminate the message
        assert(bytes_received < 256);
        recv_msg[bytes_received] = '\0';

        printf("%s", recv_msg);
        fflush(stdout);
    }

    delete[] recv_msg;
    free(sd_ptr);

    return NULL;
}

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
        perror("Unable to create socket");
        exit(1);
    }

    // connect(2) - initiate a socket connection
    error = connect(sd, res->ai_addr, res->ai_addrlen);
    if (error == -1) {
        perror("Unable to initialize connection on socket");
        exit(1);
    }

    int* _sd = new int;
    *_sd = sd;

    // Receive incoming messages on a separate thread
    pthread_t receiver_thread;
    pthread_create(&receiver_thread, NULL, receiver, (void*) _sd);

    char* send_msg;
    size_t len;

    while (true) {
        // Get input from stdin
        error = getline(&send_msg, &len, stdin);
        // Length of message read from stdin
        len = error;
        if (error == -1) {
            perror("Unable to read input with getline");
            continue;
        }

        // send(2) - send a message on the socket
        bytes_sent = send(sd,       // socket
                          send_msg, // buffer to store message to send
                          len,      // length of message
                          0);       // flags
        // TODO: Gracefully detect and handle unexpected server crashes
        //       Try checking bytes_sent == 0?
        if (bytes_sent == -1) {
            perror("Unable to send message");
            exit(1);
        }
    }

    pthread_join(receiver_thread, NULL);

    // Clean up descriptors, memory
    close(sd);
    freeaddrinfo(res);

    return 0;
}
