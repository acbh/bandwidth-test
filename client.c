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

#define SERVER_PORT 5201
#define BUFFER_SIZE 1470

int client_fd;
int is_udp;
char server_ip[INET_ADDRSTRLEN];
char mode[10] = "double";

void handle_alarm(int sig) {
    // 更新显示
    mvprintw(2, 0, "Mode: %s", mode);
    mvprintw(3, 0, "Server IP: %s", server_ip);
    mvprintw(4, 0, "Test duration: %d seconds", 60); // 示例时间
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

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <server_ip>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    strcpy(server_ip, argv[1]);

    initscr();
    cbreak();
    noecho();
    curs_set(0);
    signal(SIGALRM, handle_alarm);
    
    setup_client(1, server_ip); // 1 for UDP

    mvprintw(1, 0, "Connected to %s", server_ip);
    refresh();
    
    struct itimerval timer;
    memset(&timer, 0, sizeof(timer));
    timer.it_value.tv_sec = 1;
    timer.it_interval.tv_sec = 1;
    setitimer(ITIMER_REAL, &timer, NULL);

    while (1) {
        // 读取服务器数据并更新显示
        char buffer[BUFFER_SIZE];
        ssize_t len = recv(client_fd, buffer, BUFFER_SIZE, 0);
        if (len > 0) {
            mvprintw(6, 0, "Received %ld bytes", len);
            refresh();
        } else if (len < 0) {
            perror("recv failed");
            break;
        }
    }

    endwin();
    close(client_fd);
    return 0;
}
