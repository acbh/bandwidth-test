#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/time.h>

#define SERVER_PORT 5201
#define BUFFER_SIZE 1470

// 记录传输状态
typedef struct {
    int sockfd;
    long total_bytes_sent;
    long total_bytes_received;
    struct timeval start_time;
} transfer_info_t;

// 发送数据（上传）
void* send_data(void* arg) {
    transfer_info_t* info = (transfer_info_t*)arg;
    char buffer[BUFFER_SIZE];
    memset(buffer, 'A', sizeof(buffer));  // 填充缓冲区数据

    while (1) {
        ssize_t n = send(info->sockfd, buffer, BUFFER_SIZE, 0);  // 发送数据
        if (n <= 0) {
            break;
        }
        info->total_bytes_sent += n;  // 累积上传的字节数
    }

    return NULL;
}

// 接收数据（下载）
void* receive_data(void* arg) {
    transfer_info_t* info = (transfer_info_t*)arg;
    char buffer[BUFFER_SIZE];

    while (1) {
        ssize_t n = recv(info->sockfd, buffer, BUFFER_SIZE, 0);  // 接收数据
        if (n <= 0) {
            break;
        }
        info->total_bytes_received += n;  // 累积下载的字节数
    }

    return NULL;
}

int main() {
    int sockfd;
    struct sockaddr_in server_addr;
    struct timeval end_time;
    pthread_t send_thread, receive_thread;

    // 创建 TCP 套接字
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    // 配置服务器地址
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    // 连接服务器
    if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // 初始化传输信息
    transfer_info_t transfer_info = {
        .sockfd = sockfd,
        .total_bytes_sent = 0,
        .total_bytes_received = 0,
    };
    gettimeofday(&transfer_info.start_time, NULL);

    // 创建发送和接收数据的线程
    pthread_create(&send_thread, NULL, send_data, &transfer_info);
    pthread_create(&receive_thread, NULL, receive_data, &transfer_info);

    // 等待用户按下回车键结束测试
    printf("Press Enter to stop the test...\n");
    getchar();

    // 关闭线程
    pthread_cancel(send_thread);
    pthread_cancel(receive_thread);
    pthread_join(send_thread, NULL);
    pthread_join(receive_thread, NULL);

    // 计算带宽
    gettimeofday(&end_time, NULL);
    double elapsed_time = (end_time.tv_sec - transfer_info.start_time.tv_sec) +
                          ((end_time.tv_usec - transfer_info.start_time.tv_usec) / 1000000.0);

    double upload_bandwidth = (transfer_info.total_bytes_sent * 8.0) / elapsed_time / 1e6;
    double download_bandwidth = (transfer_info.total_bytes_received * 8.0) / elapsed_time / 1e6;

    printf("Total data sent: %ld bytes\n", transfer_info.total_bytes_sent);
    printf("Total data received: %ld bytes\n", transfer_info.total_bytes_received);
    printf("Upload Bandwidth: %.2f Mbps\n", upload_bandwidth);
    printf("Download Bandwidth: %.2f Mbps\n", download_bandwidth);

    // 关闭套接字
    close(sockfd);
    return 0;
}
