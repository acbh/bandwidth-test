#ifndef SERVER_H
#define SERVER_H

#include <arpa/inet.h>
#include <pthread.h>

#define SERVER_PORT 5201
#define MAX_CLIENTS 10

typedef enum {
    MODE_DOUBLE,
    MODE_UP,
    MODE_DOWN
} test_mode_t;

extern test_mode_t current_mode;

typedef struct {
    int fd;
    long total_bytes_up;
    long total_bytes_down;
    struct timeval start;
    pthread_mutex_t lock;
    char ip[INET_ADDRSTRLEN];
    int port;
} client_info_t;

extern client_info_t clients[MAX_CLIENTS];

int init_server(struct sockaddr_in* server_addr);
void handle_alarm(int sig);
void handle_input(int ch);

#endif // SERVER_H
