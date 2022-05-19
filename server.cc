#include <netdb.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <queue>
#include <set>

struct pending_msg {
    int source_cd; // sender client
    char* msg;     // message
    size_t len;    // message length
};

std::vector<int> sockets;
pthread_mutex_t sockets_mutex;

constexpr int N_PENDING_SOCKETS = 5;
const char* PENDING_SOCKETS_EMPTY = "pending_sockets_empty";
const char* PENDING_SOCKETS_FULL = "pending_sockets_full";

std::queue<int> pending_sockets;
pthread_mutex_t pending_sockets_mutex;
sem_t* pending_sockets_empty;
sem_t* pending_sockets_full;

constexpr int N_PENDING_MSGS = 3;
const char* PENDING_MSGS_EMPTY = "pending_msgs_empty";
const char* PENDING_MSGS_FULL = "pending_msgs_full";

std::queue<const pending_msg*> pending_msgs;
pthread_mutex_t pending_msgs_mutex;
sem_t* pending_msgs_empty;
sem_t* pending_msgs_full;

void* message_broadcaster(void* arg) {
    (void) arg;

    const pending_msg*  msg;
    ssize_t bytes_sent;

    while (true) {
        // Consume the latest message
        sem_wait(pending_msgs_full);
        pthread_mutex_lock(&pending_msgs_mutex);
        msg = pending_msgs.front();
        pending_msgs.pop();
        pthread_mutex_unlock(&pending_msgs_mutex);
        sem_post(pending_msgs_empty);

        char* formatted_msg;
        ssize_t formatted_len;

        asprintf(&formatted_msg,
                 "[Client %d] %s",
                 msg->source_cd,
                 msg->msg);
        formatted_len = strlen(formatted_msg);

        printf("[Message Broadcaster] Broadcasting message: %s\n", formatted_msg);

        pthread_mutex_lock(&sockets_mutex);
        for (int cd : sockets) {
            // Avoid broadcasting the message to the client which sent it
            if (cd == msg->source_cd) {
                continue;
            }

            bytes_sent = send(cd,            // socket
                              formatted_msg, // message
                              formatted_len, // length
                              0);            // flags
            // TODO: Gracefully detect and handle unexpected server crashes
            //       Try checking bytes_sent == 0?
            if (bytes_sent == -1) {
                pthread_mutex_unlock(&sockets_mutex);
                perror("Unable to send message");
                exit(1);
            }
        }
        pthread_mutex_unlock(&sockets_mutex);

        free(formatted_msg);
        delete[] msg->msg;
        delete msg;
    }

    return NULL;
}


void* client_handler(void* arg) {
    (void) arg;

    char* recv_msg = new char[256];
    ssize_t bytes_received;
    int cd;

    while (true) {
        // Consume the latest socket
        sem_wait(pending_sockets_full);
        pthread_mutex_lock(&pending_sockets_mutex);
        cd = pending_sockets.front();
        pending_sockets.pop();
        pthread_mutex_unlock(&pending_sockets_mutex);
        sem_post(pending_sockets_empty);

        // recv(2) - receive a message on the socket
        bytes_received = recv(cd,       // socket
                              recv_msg, // buffer to store received message
                              256,      // max length of buffer
                              0);       // flags
        if (bytes_received == 0) {
            printf("[Client Handler] Client %d has closed the connection\n", cd);

            pthread_mutex_lock(&sockets_mutex);
            sockets.erase(std::find(sockets.begin(), sockets.end(), cd));
            pthread_mutex_unlock(&sockets_mutex);

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

        printf("[Client Handler] Handled message from %d: %s\n", cd, recv_msg);

        pending_msg* msg = new pending_msg;
        msg->source_cd = cd;
        msg->msg = new char[256];
        msg->len = bytes_received;
        strcpy(msg->msg, recv_msg);

        // Signal to message broadcaster threads
        sem_wait(pending_msgs_empty);
        pthread_mutex_lock(&pending_msgs_mutex);
        pending_msgs.push(msg);
        pthread_mutex_unlock(&pending_msgs_mutex);
        sem_post(pending_msgs_full);
    }

    return NULL;
}

void* poller(void* arg) {
    (void) arg;

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
        timeout.tv_sec = 0;
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

                    // Signal to client handler threads
                    sem_wait(pending_sockets_empty);
                    pthread_mutex_lock(&pending_sockets_mutex);
                    pending_sockets.push(cd);
                    pthread_mutex_unlock(&pending_sockets_mutex);
                    sem_post(pending_sockets_full);
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
    pthread_mutex_init(&pending_sockets_mutex, NULL);
    pthread_mutex_init(&pending_msgs_mutex, NULL);

    sem_unlink(PENDING_SOCKETS_EMPTY);
    sem_unlink(PENDING_SOCKETS_FULL);
    pending_sockets_empty = sem_open(PENDING_SOCKETS_EMPTY,
                                     O_CREAT,
                                     0,
                                     N_PENDING_SOCKETS);
    pending_sockets_full = sem_open(PENDING_SOCKETS_FULL,
                                    O_CREAT,
                                    0,
                                    0);

    sem_unlink(PENDING_MSGS_EMPTY);
    sem_unlink(PENDING_MSGS_FULL);
    pending_msgs_empty = sem_open(PENDING_MSGS_EMPTY,
                                  O_CREAT,
                                  0,
                                  N_PENDING_MSGS);
    pending_msgs_full = sem_open(PENDING_MSGS_FULL,
                                 O_CREAT,
                                 0,
                                 0);

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

    // Initialize message broadcaster thread
    pthread_t message_broadcaster_thread;
    pthread_create(&message_broadcaster_thread, NULL, message_broadcaster, NULL);

    // Initialize client handler thread
    // TODO: Make this a configurable thread pool
    pthread_t client_handler_thread;
    pthread_create(&client_handler_thread, NULL, client_handler, NULL);

    // Initialize poller thread
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

    // TODO: Handle exits cleanly (e.g., ^C)
    // Clean up descriptors, memory
    close(sd);
    freeaddrinfo(res);
    pthread_mutex_destroy(&sockets_mutex);
    pthread_mutex_destroy(&pending_sockets_mutex);
    pthread_mutex_destroy(&pending_msgs_mutex);

    printf("[Main] Clean up complete\n");

    return 0;
}
