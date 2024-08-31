#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define SERVER_PORT 5201
#define BUFFER_SIZE 1470

int main() {
    int sockfd;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];
    struct timeval start, end;

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

    // 填充数据缓冲区
    memset(buffer, 'A', sizeof(buffer));

    // 开始计时
    gettimeofday(&start, NULL);

    // 连续发送数据包
    long total_bytes_sent = 0;
    while (1) {
        ssize_t n = send(sockfd, buffer, BUFFER_SIZE, 0);
        if (n <= 0) {
            break;
        }
        total_bytes_sent += n;

        // 假设测试时间为10秒
        gettimeofday(&end, NULL);
        double elapsed_time = (end.tv_sec - start.tv_sec) + ((end.tv_usec - start.tv_usec) / 1000000.0);
        // if (elapsed_time >= 10.0) {
        //     break;
        // }
    }

    // 计算带宽
    double bandwidth_mbps = (total_bytes_sent * 8.0) / ((end.tv_sec - start.tv_sec) + ((end.tv_usec - start.tv_usec) / 1000000.0)) / 1e6;
    printf("Total data sent: %ld bytes\n", total_bytes_sent);
    printf("Bandwidth: %.2f Mbps\n", bandwidth_mbps);

    close(sockfd);
    return 0;
}