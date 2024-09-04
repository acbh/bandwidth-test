#ifndef CLIENT_HANDLER_H
#define CLIENT_HANDLER_H

#include <pthread.h>
#include <arpa/inet.h>
#include "server.h"

void* handle_client(void* arg);

#endif // CLIENT_HANDLER_H
