#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/time.h>

#define SERVER_PORT 5201
#define BUFFER_SIZE 1470

int running = 1;  // 控制上传和下载线程的运行状态

void* client_upload(void* arg) {
    int sockfd = *((int*)arg);
    char buffer[BUFFER_SIZE];
    memset(buffer, 'U', BUFFER_SIZE);  // 上传时发送的填充数据

    while (running) {
        ssize_t len = send(sockfd, buffer, BUFFER_SIZE, 0);
        if (len <= 0) {
            perror("Failed to send data to server");
            break;
        }
        // 你可以加入速率限制的逻辑，例如 usleep()
    }
    pthread_exit(NULL);
}

void* client_download(void* arg) {
    int sockfd = *((int*)arg);
    char buffer[BUFFER_SIZE];

    while (running) {
        ssize_t len = recv(sockfd, buffer, BUFFER_SIZE, 0);
        if (len <= 0) {
            perror("Failed to receive data from server");
            break;
        }
        // 接收到的数据可以在此进行处理
    }
    pthread_exit(NULL);
}

int main(int argc, char* argv[]) {
    char* server_ip;
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <server_ip>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    server_ip = argv[1];

    // 创建 socket
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // 配置服务器地址
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        perror("Invalid server IP address");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // 连接服务器
    if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection to server failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    printf("Connected to server %s on port %d\n", server_ip, SERVER_PORT);

    // 启动上传和下载线程
    pthread_t upload_thread, download_thread;
    pthread_create(&upload_thread, NULL, client_upload, &sockfd);
    pthread_create(&download_thread, NULL, client_download, &sockfd);

    // 等待用户按下回车键来终止客户端
    printf("Press Enter to stop the test...\n");
    getchar();
    running = 0;  // 停止上传和下载线程

    // 等待线程结束
    pthread_join(upload_thread, NULL);
    pthread_join(download_thread, NULL);

    // 关闭 socket
    close(sockfd);
    printf("Client stopped\n");

    return 0;
}
