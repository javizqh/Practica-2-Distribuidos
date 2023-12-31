/**
 * @file stub.c
 * @author Javier Izquierdo Hernandez (javizqh@gmail.com)
 * @brief stub de j.izquierdoh.2021@alumnos.urjc.es
 * @version 1.0
 * @date 2023-10-18
 * 
 * @copyright Copyright (c) 2023
 * 
 */
#include "stub.h"

#define SERVER_ID 2
#define N_CLIENTS 2
#define MESSAGE_MAX_SIZE 20
#define ZERO_ASCII 48
#define BROADCAST -1

#define ERROR(msg) fprintf(stderr,"STUB ERROR: %s\n",msg)

// ------------------- Private structs and enums declarations -----------------
enum operations {
    READY_TO_SHUTDOWN = 0,
    SHUTDOWN_NOW,
    SHUTDOWN_ACK
};

enum socket_operation {
    SEND,
    RECEIVE
};

// Info to manage multiple connections from server
struct accept_info {
    int thread_id;
    struct sockaddr *__addr;
    socklen_t __len;
};

struct client_info {
    int id;
    int socket_fd;
    pthread_t recv_thread;
    pthread_t accept_thread;
    enum operations curr_operation;
};

struct message {
    char origin[MESSAGE_MAX_SIZE];
    enum operations action;
    unsigned int clock_lamport;
};
// --------------- End of private structs and enums declarations --------------
// --------------------- Private variables declarations -----------------------
// For both client and server
int proc_id;
int socket_fd;
int lamport_clock = 0;
int n_threads = 0;
enum operations curr_operation;
// Only for clients
pthread_t recv_thread;
// Only for server
int n_clients_accepted = 0;
struct accept_info accept_info_threads[N_CLIENTS];
struct client_info server_fd[N_CLIENTS];
// ------------------ End of private variables declarations -------------------
// --------------------- Private functions declarations -----------------------
void update_clock_lamport(unsigned int clock_lamport);
int close_network(int id);
int send_message(int dest_id, enum operations action);
int recv_message(int dest_id);
enum operations get_message_info(int dest_id);
uint16_t get_port_from_string(const char *input);
int init_client(int sockfd, struct sockaddr *__addr, socklen_t __len);
int init_server(int sockfd, struct sockaddr *__addr, socklen_t __len);
int wait_to_shutdown();
void * accept_client_thread(void *arg);
void * recv_message_thread(void *arg);
void print_info(char message[20], enum socket_operation operation, int action);
// ------------------ End of private functions declarations -------------------


int get_clock_lamport() {
    return lamport_clock;
}

int init_process(int id, const char *ip, const char *port_in) {
    struct in_addr addr;
    struct sockaddr_in servaddr;
    int tcp_socket;
    uint16_t port;
    socklen_t len;

    proc_id = id;

    port = get_port_from_string(port_in);

    if (inet_pton(AF_INET, ip, &addr) < 0) {
        return 0;
    }

    setbuf(stdout, NULL);

    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = addr.s_addr;
    servaddr.sin_port = htons(port);

    len = sizeof(servaddr);

    tcp_socket = socket(AF_INET, SOCK_STREAM, 0);

    if (tcp_socket < 0) {
        fprintf(stderr, "Socket failed\n");
        return 0;
    }

    #ifdef DEBUG
        printf("Socket successfully created...\n");
    #endif

    if (id == SERVER_ID) {
        socket_fd = init_server(tcp_socket, (struct sockaddr *)&servaddr, len);
    } else {
        socket_fd = init_client(tcp_socket, (struct sockaddr *)&servaddr, len);
    }

    return socket_fd > 0;
}

int ready_to_shutdown() {
    send_message(2,READY_TO_SHUTDOWN);
    recv_message(2);
    return 1;
}

int shutdown_proc() {
    if (get_message_info(2) != SHUTDOWN_NOW) {
        #ifdef DEBUG
            printf("Error: incorrect message\n");
        #endif
    }
    send_message(2,SHUTDOWN_ACK);
    close_network(proc_id);
    return 1;
}

int shutdown_to(int id) {
    if (get_message_info(id) != READY_TO_SHUTDOWN) {
        #ifdef DEBUG
            printf("Error: incorrect message\\n");
        #endif
    }
    send_message(id,SHUTDOWN_NOW);
    recv_message(id);
    return 1;
}

int is_all_shutdown() {
    close_network(proc_id);
    return get_message_info(3) == SHUTDOWN_ACK && 
           get_message_info(1) == SHUTDOWN_ACK;
}

// ------------------------ Private functions ---------------------------------
// -------------------- Initialize socket function ----------------------------

int init_client(int sockfd, struct sockaddr *__addr, socklen_t __len) {
    if (connect(sockfd, __addr, __len) < 0){
        ERROR("Unable to connect");
        return 0;
    }

    #ifdef DEBUG
        printf("Connected to the server...\n");
    #endif
    return sockfd;
}

int init_server(int sockfd, struct sockaddr *__addr, socklen_t __len) {
    const int enable = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR,
        &enable, sizeof(int)) < 0) {
        ERROR("Setsockopt(SO_REUSEADDR) failed");
        return 0;
    }

    if (bind(sockfd, __addr, __len) < 0) {
        ERROR("Unable to bind");
        return 0;
    }

    #ifdef DEBUG
        printf("Socket successfully binded...\n");
    #endif

    if (listen(sockfd, 5) < 0) {
        ERROR("Unable to listen");
        return 0;
    }

    #ifdef DEBUG
        printf("Server listening...\n");
    #endif

    socket_fd = sockfd;
    // Wait for both clients to connect
    for (int i = 0; i < N_CLIENTS; i++) {
        accept_info_threads[i].thread_id = i;
        accept_info_threads[i].__addr = __addr;
        accept_info_threads[i].__len = __len;
        pthread_create(&server_fd[i].accept_thread, NULL, accept_client_thread,
                       (void *) &accept_info_threads[i]);
    }

    wait_to_shutdown();

    return sockfd;
}

int wait_to_shutdown() {
    recv_message(BROADCAST);
    recv_message(BROADCAST);
}

// ----------------------------------------------------------------------------

// ------------------------- Accept function ----------------------------------

void * accept_client_thread(void *arg) {
    struct accept_info info = *(struct accept_info *) arg;
    if ((server_fd[info.thread_id].socket_fd = 
         accept(socket_fd, info.__addr, &info.__len)) < 0) {
        ERROR("failed to accept socket");
    }
    pthread_exit(NULL);
}

// ----------------------------------------------------------------------------

// ------------------------- Recieve functions --------------------------------

int recv_message(int dest_id) {
    // Create a thread for each one
    int id = dest_id;

    n_threads++;
    if (n_threads > N_CLIENTS) {
        #ifdef DEBUG
            fprintf(stderr, "More threads than clients connected\n");
        #endif
        return 0;
    }

    if (proc_id == SERVER_ID) {
        // Get corresponding socket_fd from server_fd, if id is broadcast
        // assign a broadcast id and wait for a response to load the id
        for (size_t i = 0; i < N_CLIENTS; i++) {
            if (id == BROADCAST) {
                if (server_fd[i].id == 0) {
                    // The client does not have a real id
                    server_fd[i].id = BROADCAST - i;
                    pthread_create(&server_fd[i].recv_thread, NULL, 
                                recv_message_thread,(void *) &server_fd[i].id);
                    break;
                }
            } else if (server_fd[i].id == id) {
                pthread_create(&server_fd[i].recv_thread, NULL, 
                            recv_message_thread,(void *) &server_fd[i].id);
                break;
            }
        }
    } else {
        pthread_create(&recv_thread, NULL, recv_message_thread,
                       (void *) &dest_id);
    }

    return 1;
}

void * recv_message_thread(void *arg) {
    ssize_t bytes_recv;
    int dest_id = *(int *)arg;
    struct message msg;

    if (proc_id == SERVER_ID) {
        // Get corresponding socket_fd from server_fd, if no ids wait for 
        // response to load
        if (dest_id <= BROADCAST) {
            pthread_join(server_fd[BROADCAST - dest_id].accept_thread , 
                         NULL);
            bytes_recv = recv(server_fd[BROADCAST - dest_id].socket_fd,
                              &msg, sizeof(msg), MSG_WAITALL);
            server_fd[BROADCAST - dest_id].id = msg.origin[1] - ZERO_ASCII;
            server_fd[BROADCAST - dest_id].curr_operation = msg.action;
        } else {
            for (size_t i = 0; i < N_CLIENTS; i++) {
                if (server_fd[i].id == dest_id) {
                    bytes_recv = recv(server_fd[i].socket_fd, &msg, sizeof(msg),
                                      MSG_WAITALL);
                    server_fd[i].curr_operation = msg.action;
                }
            }
        }
    } else {
        bytes_recv = recv(socket_fd, &msg, sizeof(msg), MSG_WAITALL);
        curr_operation = msg.action;
    }

    if (bytes_recv < 0) {
        ERROR("Failed to receive");
    } else {
        update_clock_lamport(msg.clock_lamport);
        print_info(msg.origin, RECEIVE, msg.action);
    }

    pthread_exit(NULL);
}

enum operations get_message_info(int dest_id) {
    if (proc_id == SERVER_ID) {
        for (size_t i = 0; i < N_CLIENTS; i++) {
            if (server_fd[i].id == dest_id) {
               pthread_join(server_fd[i].recv_thread ,NULL);
               n_threads--;
               return server_fd[i].curr_operation;
            }
        }
    } else {
        pthread_join(recv_thread ,NULL);
        n_threads--;
        return curr_operation;
    }
}

// ----------------------------------------------------------------------------

// --------------------------- Send function ----------------------------------

int send_message(int id, enum operations action) {
    ssize_t bytes_sent;
    struct message msg;

    msg.action = action;
    msg.origin[0] = 'P';
    msg.origin[1] = ZERO_ASCII + proc_id;
    msg.origin[2] = '\0';
    lamport_clock ++;
    msg.clock_lamport = lamport_clock;
    print_info(msg.origin, SEND, msg.action);

    if (proc_id == SERVER_ID) {
        for (size_t i = 0; i < N_CLIENTS; i++) {
            if (server_fd[i].id == id) {
                bytes_sent = send(server_fd[i].socket_fd, &msg, sizeof(msg),
                                  MSG_WAITALL);
                break;
            }
        }
    } else {
        bytes_sent = send(socket_fd, &msg, sizeof(msg), MSG_WAITALL);
    }
    if (bytes_sent < 0) {
        ERROR("send failed");
    }

    return bytes_sent;
}

// ----------------------------------------------------------------------------

// ------------------------ Close socket function -----------------------------

int close_network(int id) {
    if (id == SERVER_ID) {
        for (int i = 0; i < N_CLIENTS; i++) {
            if (close (server_fd[i].socket_fd) < 0) {
                ERROR("Failed to close socket");
                return 0;
            }
        }
    } else {
        if (close (socket_fd) < 0) {
            ERROR("Failed to close socket");
            return 0;
        }
    }
    #ifdef DEBUG
        printf("Network connection closed\n");
    #endif
    return 1;
}

// ----------------------------------------------------------------------------

// ------------------------- Utility functions --------------------------------

void update_clock_lamport(unsigned int clock_lamport) {
    if (clock_lamport > lamport_clock) {
        lamport_clock = clock_lamport + 1;
    } else {
        lamport_clock++;
    }
}

uint16_t get_port_from_string(const char *input) {
    char *end;
    long val;
    
    val = strtol(input, &end, 10);
    if (end == input || *end != '\0' || val < 0 || val >= 0x10000) {
        return 0;
    }
    return (uint16_t)val;
}

void print_info(char message[MESSAGE_MAX_SIZE], 
                enum socket_operation operation, int action) {
    switch (operation) {
    case SEND:
        printf("%s, %d, SEND, ", message, lamport_clock);
        break;
    case RECEIVE:
        printf("P%d, %d, RECV (%s), ", proc_id, lamport_clock, message);
        break;
    default:
        printf("NOT SUPPORTED OPERATION, ");
        break;
    }
    switch (action) {
    case READY_TO_SHUTDOWN:
        printf("READY_TO_SHUTDOWN\n");
        break;
    case RECEIVE:
        printf("SHUTDOWN_NOW\n");
        break;
    default:
        printf("SHUTDOWN_ACK\n");
        break;
    }
}

// ----------------------------------------------------------------------------
// --------------------- End of private functions -----------------------------
