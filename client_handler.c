#include "client_handler.h"

void* handle_client(void* arg) {
    client_info_t* client = (client_info_t*)arg;
    char buffer[BUFFER_SIZE];
    ssize_t len;

    while ((len = recv(client->fd, buffer, BUFFER_SIZE, 0)) > 0) {
        pthread_mutex_lock(&client->lock);

        if (current_mode == MODE_DOUBLE || current_mode == MODE_UP) {
            client->total_bytes_up += len;
        } else if (current_mode == MODE_DOWN) {
            client->total_bytes_down += len;
        }

        pthread_mutex_unlock(&client->lock);
    }

    close(client->fd);
    pthread_exit(NULL);
}
