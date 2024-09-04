#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <signal.h>
#include <ncurses.h>
#include <ctype.h>

#define SERVER_PORT 5201
#define BUFFER_SIZE 1470
#define MAX_CLIENTS 10

typedef enum {
    MODE_DOUBLE,
    MODE_UP,
    MODE_DOWN
} test_mode_t;

WINDOW *main_win; // ncurses窗口指针
test_mode_t current_mode = MODE_DOUBLE; // 当前模式
int speed_limit = -1; // 初始没有限速

// 记录每个客户端的状态
typedef struct {
    int fd;                // 客户端套接字
    long total_bytes_up;   // 累计上行数据量
    long total_bytes_down; // 累计下行数据量
    struct timeval start;  // 统计开始时间
    pthread_mutex_t lock;  // 锁用于线程安全
    char ip[INET_ADDRSTRLEN];
    int port;
} client_info_t;

client_info_t clients[MAX_CLIENTS];

const char* get_mode_string(test_mode_t mode) {
    switch (mode) {
        case MODE_DOUBLE: return "double";
        case MODE_UP: return "UP";
        case MODE_DOWN: return "DOWN";
        default: return "unknown";
    }
}

void handle_alarm(int sig) {
    double bandwidth_mbps;
    struct timeval now, elapsed;

    gettimeofday(&now, NULL);
    int rank = 1;  // 用于显示的排名

    for (size_t i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].fd != 0) { // 确保该槽位已经分配了客户端
            pthread_mutex_lock(&clients[i].lock);

            timersub(&now, &clients[i].start, &elapsed);  // 计算时间差
            double elapsed_time = elapsed.tv_sec + elapsed.tv_usec / 1000000.0;

            if (elapsed_time > 0) {
                if (current_mode == MODE_DOUBLE || current_mode == MODE_UP) {
                    bandwidth_mbps = (clients[i].total_bytes_up * 8.0) / elapsed_time / 1e6;
                } else if (current_mode == MODE_DOWN) {
                    bandwidth_mbps = (clients[i].total_bytes_down * 8.0) / elapsed_time / 1e6;
                } else {
                    bandwidth_mbps = 0;
                }
            } else {
                bandwidth_mbps = 0;
            }

            if (clients[i].total_bytes_up > 0 || bandwidth_mbps > 0) {
                mvwprintw(main_win, rank + 10, 1, "| [%2d] | %s\t|  %d | %8.2f Mbps |", 
                    rank, clients[i].ip, clients[i].port, bandwidth_mbps);
                rank++;
            }

            wrefresh(main_win);

            if (elapsed_time >= 1.0) {
                clients[i].total_bytes_up = 0;
                clients[i].total_bytes_down = 0;
                gettimeofday(&clients[i].start, NULL);
            }

            pthread_mutex_unlock(&clients[i].lock);
        }
    }

    for (int j = rank; j <= MAX_CLIENTS; j++) {
        mvwprintw(main_win, j + 10, 1, "|      | \t\t|        | \t\t | \t\t |");
    }
    wrefresh(main_win);
}

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

void handle_mouse_event(MEVENT* event) {
    if (event->y == 4 && event->x >= 7 && event->x <= 13) { // 模式切换区域
        switch (current_mode) {
            case MODE_DOUBLE: current_mode = MODE_UP; break;
            case MODE_UP: current_mode = MODE_DOWN; break;
            case MODE_DOWN: current_mode = MODE_DOUBLE; break;
        }
        mvwprintw(main_win, 4, 1, "Mode:    %s \t\t\t\tLimit: %s", get_mode_string(current_mode), speed_limit == -1 ? "no limit" : "limited");
        wrefresh(main_win);
    } else if (event->y == 6 && event->x >= 9 && event->x <= 20) { // 限速设置区域
        char limit_str[10];
        mvwprintw(main_win, 7, 1, "Enter new limit (e.g., 10 for 10%%): ");
        wrefresh(main_win);
        echo();
        wgetnstr(main_win, limit_str, sizeof(limit_str) - 1);
        noecho();
        speed_limit = atoi(limit_str);
        mvwprintw(main_win, 4, 1, "Mode:    %s \t\t\t\tLimit: %s", get_mode_string(current_mode), speed_limit == -1 ? "no limit" : limit_str);
        wrefresh(main_win);
    }
}

int main(int argc, char *argv[]) {
    int server_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    pthread_t threads[MAX_CLIENTS];

    memset(clients, 0, sizeof(clients));

    if (argc < 5) {
        fprintf(stderr, "Usage: %s -p <protocol> -m <mode>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    char *protocol = NULL;
    char *mode = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0) {
            protocol = argv[++i];
        } else if (strcmp(argv[i], "-m") == 0) {
            mode = argv[++i];
        }
    }

    if (protocol == NULL || mode == NULL) {
        fprintf(stderr, "Usage: %s -p <protocol> -m <mode>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int is_udp = strcmp(protocol, "udp") == 0;
    int is_up = strcmp(mode, "up") == 0;
    int is_down = strcmp(mode, "down") == 0;
    int is_double = strcmp(mode, "double") == 0;

    if (is_udp && is_up) {
        current_mode = MODE_UP;
    } else if (is_udp && is_down) {
        current_mode = MODE_DOWN;
    } else if (is_udp && is_double) {
        current_mode = MODE_DOUBLE;
    } else {
        fprintf(stderr, "Invalid mode or protocol\n");
        exit(EXIT_FAILURE);
    }

    if ((server_fd = socket(AF_INET, is_udp ? SOCK_DGRAM : SOCK_STREAM, 0)) < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (const struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    if (!is_udp && listen(server_fd, MAX_CLIENTS) < 0) {
        perror("listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    initscr();
    cbreak();
    noecho();
    curs_set(0);
    mousemask(ALL_MOUSE_EVENTS, NULL);

    main_win = newwin(MAX_CLIENTS * 2 + 4, 80, 0, 0);
    box(main_win, 0, 0);
    mvwprintw(main_win, 1, 1, "Server listening on port %d...", SERVER_PORT);
    mvwprintw(main_win, 2, 1, "Server    addr: \t192.168.18.125\t\tport: 8888");
    mvwprintw(main_win, 3, 1, "Broadcast addr: \t192.168.18.255\t\tport: 5005");
    mvwprintw(main_win, 4, 1, "Mode:    %s \t\t\t\tLimit: %s", get_mode_string(current_mode), speed_limit == -1 ? "no limit" : "limited");
   
    mvwprintw(main_win, 5, 1, "Max Bandwidth: \t\t\t\t\t\t\t");
    mvwprintw(main_win, 6, 1, "Client Info:\n");
    wrefresh(main_win);

    struct itimerval timer;
    memset(&timer, 0, sizeof(timer));
    timer.it_value.tv_sec = 1;
    timer.it_interval.tv_sec = 1;

    signal(SIGALRM, handle_alarm);
    setitimer(ITIMER_REAL, &timer, NULL);

    if (is_udp) {
        // UDP Server Loop
        while (1) {
            char buffer[BUFFER_SIZE];
            ssize_t len = recvfrom(server_fd, buffer, BUFFER_SIZE, 0, (struct sockaddr*)&client_addr, &client_addr_len);
            if (len > 0) {
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (clients[i].fd == 0) {
                        clients[i].fd = server_fd;
                        inet_ntop(AF_INET, &(client_addr.sin_addr), clients[i].ip, INET_ADDRSTRLEN);
                        clients[i].port = ntohs(client_addr.sin_port);
                        gettimeofday(&clients[i].start, NULL);
                        pthread_create(&threads[i], NULL, handle_client, (void*)&clients[i]);
                        break;
                    }
                }
            }
        }
    } else {
        // TCP Server Loop
        while (1) {
            int new_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_addr_len);
            if (new_fd >= 0) {
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (clients[i].fd == 0) {
                        clients[i].fd = new_fd;
                        inet_ntop(AF_INET, &(client_addr.sin_addr), clients[i].ip, INET_ADDRSTRLEN);
                        clients[i].port = ntohs(client_addr.sin_port);
                        gettimeofday(&clients[i].start, NULL);
                        pthread_create(&threads[i], NULL, handle_client, (void*)&clients[i]);
                        break;
                    }
                }
            }
        }
    }

    endwin();
    close(server_fd);
    return 0;
}
