#ifndef CLIENT_HANDLER_H
#define CLIENT_HANDLER_H

#include <pthread.h>

typedef struct {
    int fd;
    long total_bytes_up;
    long total_bytes_down;
    pthread_mutex_t lock;
    double limit_factor;
} client_info_t;

void* handle_client(void* arg);

#endif
