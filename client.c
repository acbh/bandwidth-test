#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <pthread.h>

#define SERVER_PORT 5201
#define BUFFER_SIZE 1470

pthread_mutex_t send_lock;

void* send_data(void* arg) {
    int sockfd = *((int*)arg);
    char buffer[BUFFER_SIZE];
    ssize_t len;

    memset(buffer, 'A', BUFFER_SIZE);

    while (1) {
        pthread_mutex_lock(&send_lock);
        len = send(sockfd, buffer, BUFFER_SIZE, 0);
        pthread_mutex_unlock(&send_lock);

        if (len <= 0) {
            perror("send failed");
            break;
        }

        usleep(1000); // 控制发送
    }

    close(sockfd);
    pthread_exit(NULL);
}

void* receive_data(void* arg) {
    int sockfd = *((int*)arg);
    char buffer[BUFFER_SIZE];
    ssize_t len;

    long total_bytes_received = 0;
    struct timeval start, now, elapsed;
    gettimeofday(&start, NULL);

    while (1) {
        len = recv(sockfd, buffer, BUFFER_SIZE, 0);
        if (len <= 0) {
            perror("send failed");
            break;
        }

        total_bytes_received += len;

        gettimeofday(&now, NULL);
        timersub(&now, &start, &elapsed);

        // 计算下行带宽
        double elapsed_time = elapsed.tv_sec + elapsed.tv_usec / 1000000.0;
        if (elapsed_time > 0) {
        double bandwidth_down_mbps = (total_bytes_received * 8.0) / elapsed_time /  1e6;
        
        // 发送下行带宽到服务器
        pthread_mutex_lock(&send_lock);
        char msg[256];
        snprintf(msg, sizeof(msg), "DOWN_BANDWIDTH %.2f Mbps", bandwidth_down_mbps);
        send(sockfd, msg, strlen(msg), 0);
        pthread_mutex_unlock(&send_lock);
        }
    }

    close(sockfd);
    pthread_exit(NULL);
}

int main(int argc, char* argv[]) {
    int sockfd;
    struct sockaddr_in server_addr;

    // 创建 TCP 套接字
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    // 配置服务器地址
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    // server_addr.sin_addr.s_addr = inet_addr("192.168.18.125");

    // 命令行指定服务器地址
    if (argc > 1) {
        server_addr.sin_addr.s_addr = inet_addr(argv[1]);
    } else {
        // UDP broadcast
        server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    }

    // 连接服务器
    if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    pthread_t send_thread, receive_thread;

    if (pthread_create(&send_thread, NULL, send_data, &sockfd) != 0) {
        perror("pthread create for send_data failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    if (pthread_create(&receive_thread, NULL, receive_data, &sockfd) != 0) {
        perror("pthread_create for receive_data failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    pthread_join(send_thread, NULL);
    pthread_join(receive_thread, NULL);
    pthread_mutex_destroy(&send_lock);

    close(sockfd);
    return 0;
}
