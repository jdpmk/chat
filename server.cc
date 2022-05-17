#include <netdb.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <set>
#include <vector>

std::vector<int> sockets;
pthread_mutex_t sockets_mutex;

// TODO: Use a queue as the buffer
constexpr int N_PENDING_SOCKETS = 5;
std::vector<int> pending_sockets;
pthread_mutex_t pending_sockets_mutex;
sem_t* pending_sockets_empty;
sem_t* pending_sockets_full;

// TODO: Use a queue as the buffer
// constexpr int N_PENDING_MSGS = 5;
std::vector<const char*> pending_msgs;
pthread_mutex_t pending_msgs_mutex;
sem_t* pending_msgs_empty;
sem_t* pending_msgs_full;

void* client_handler(void* arg) {
    (void) arg;

    char* recv_msg = new char[256];
    ssize_t bytes_received;
    int cd;

    while (true) {
        // TODO: Debug another message being sent before this resolves

        // Consume the latest socket
        sem_wait(pending_sockets_full);
        pthread_mutex_lock(&pending_sockets_mutex);
        cd = pending_sockets.back();
        pending_sockets.pop_back();
        pthread_mutex_unlock(&pending_sockets_mutex);
        sem_post(pending_sockets_empty);

        // recv(2) - receive a message on the socket
        bytes_received = recv(cd,       // socket
                              recv_msg, // buffer to store received message
                              256,      // max length of buffer
                              0);       // flags
        if (bytes_received == 0) {
            printf("[%d] Client has closed the connection\n", cd);

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

                    // Signal to client handler threads
                    sem_wait(pending_sockets_empty);
                    pthread_mutex_lock(&pending_sockets_mutex);
                    pending_sockets.push_back(cd);
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

    // TODO: Move names to constants
    sem_unlink("pending_sockets_empty");
    sem_unlink("pending_sockets_full");
    pending_sockets_empty = sem_open("pending_sockets_empty",
                                     O_CREAT,
                                     0,
                                     N_PENDING_SOCKETS);
    pending_sockets_full = sem_open("pending_sockets_full",
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

    // Initialize poller thread
    pthread_t poller_thread;
    pthread_create(&poller_thread, NULL, poller, NULL);

    // Initialize client handler thread
    // TODO: Make this a configurable thread pool
    pthread_t client_handler_thread;
    pthread_create(&client_handler_thread, NULL, client_handler, NULL);

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
    pthread_mutex_destroy(&pending_sockets_mutex);
    pthread_mutex_destroy(&pending_msgs_mutex);

    printf("[Main] Clean up complete\n");

    return 0;
}
