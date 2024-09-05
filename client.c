#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <ncurses.h>
#include <signal.h>
#include <errno.h>
#include <sys/time.h>

#define SERVER_PORT 5201
#define BUFFER_SIZE 1470

int client_fd;
int is_udp;
char server_ip[INET_ADDRSTRLEN] = "127.0.0.1";  // 使用环回地址
char mode[10] = "double";  // 默认模式为双向测试

long total_bytes_sent = 0;      // 累计发送的字节数
long total_bytes_received = 0;  // 累计接收的字节数
long last_bytes_sent = 0;       // 上一秒发送的字节数
long last_bytes_received = 0;   // 上一秒接收的字节数

void handle_alarm(int sig) {
    // 计算发送和接收的带宽
    long bytes_sent_this_second = total_bytes_sent - last_bytes_sent;
    long bytes_received_this_second = total_bytes_received - last_bytes_received;
    
    double upload_speed_mbps = (bytes_sent_this_second * 8.0) / (1024 * 1024);     // 上传速度 Mbps
    double download_speed_mbps = (bytes_received_this_second * 8.0) / (1024 * 1024); // 下载速度 Mbps
    
    last_bytes_sent = total_bytes_sent;
    last_bytes_received = total_bytes_received;

    // 更新显示
    mvprintw(2, 0, "Mode: %s", mode);
    mvprintw(3, 0, "Server IP: %s", server_ip);
    mvprintw(4, 0, "Test duration: ongoing");  // 持续测试
    mvprintw(5, 0, "Upload speed: %.2f Mbps", upload_speed_mbps);
    mvprintw(6, 0, "Download speed: %.2f Mbps", download_speed_mbps);
    refresh();
}

void setup_client(int is_udp, const char* ip) {
    struct sockaddr_in server_addr;
    
    client_fd = socket(AF_INET, is_udp ? SOCK_DGRAM : SOCK_STREAM, 0);
    if (client_fd < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    if (inet_pton(AF_INET, ip, &server_addr.sin_addr) <= 0) {
        perror("invalid address/ address not supported");
        exit(EXIT_FAILURE);
    }

    if (!is_udp && connect(client_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("connection failed");
        exit(EXIT_FAILURE);
    }
}

void run_test() {
    char buffer[BUFFER_SIZE];
    
    while (1) {
        if (strcmp(mode, "up") == 0 || strcmp(mode, "double") == 0) {
            // 进行上行测试
            memset(buffer, 'A', BUFFER_SIZE);  // 发送数据
            ssize_t sent_bytes = is_udp ? sendto(client_fd, buffer, BUFFER_SIZE, 0, NULL, 0) : send(client_fd, buffer, BUFFER_SIZE, 0);
            if (sent_bytes < 0) {
                perror("send failed");
                break;
            }
            total_bytes_sent += sent_bytes;
            mvprintw(8, 0, "Sent %ld bytes", sent_bytes);
            refresh();
        }
        
        if (strcmp(mode, "down") == 0 || strcmp(mode, "double") == 0) {
            // 进行下行测试
            ssize_t len = is_udp ? recvfrom(client_fd, buffer, BUFFER_SIZE, 0, NULL, NULL) : recv(client_fd, buffer, BUFFER_SIZE, 0);
            if (len > 0) {
                total_bytes_received += len;
                mvprintw(9, 0, "Received %ld bytes", len);
                refresh();
            } else if (len < 0) {
                perror("recv failed");
                break;
            }
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc > 1) {
        strncpy(server_ip, argv[1], INET_ADDRSTRLEN);
    }

    initscr();
    cbreak();
    noecho();
    curs_set(0);
    signal(SIGALRM, handle_alarm);
    
    setup_client(1, server_ip); // 1表示UDP，改成0表示TCP

    mvprintw(1, 0, "Connected to %s", server_ip);
    refresh();
    
    struct itimerval timer;
    memset(&timer, 0, sizeof(timer));
    timer.it_value.tv_sec = 1;
    timer.it_interval.tv_sec = 1;
    setitimer(ITIMER_REAL, &timer, NULL);

    // 开始测试
    run_test();

    endwin();
    close(client_fd);
    return 0;
}
