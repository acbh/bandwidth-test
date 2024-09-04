#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <signal.h>
#include <ncurses.h>

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

// 记录每个客户端的状态
typedef struct {
    int fd;                // 客户端套接字
    long total_bytes_up;   // 累计上行数据量
    long total_bytes_down; // 累计下行数据量（如果需要）
    struct timeval start;  // 统计开始时间
    pthread_mutex_t lock;  // 锁用于线程安全
    char ip[INET_ADDRSTRLEN];
    int port;
} client_info_t;

client_info_t clients[MAX_CLIENTS];

// 获取模式的字符串表示
const char* get_mode_string(test_mode_t mode) {
    switch (mode) {
        case MODE_DOUBLE: return "double";
        case MODE_UP: return "UP";
        case MODE_DOWN: return "DOWN";
        default: return "unknown";
    }
}

// 计算带宽并在ncurses窗口中显示
void handle_alarm(int sig) {
    double bandwidth_mbps;
    struct timeval now, elapsed;

    gettimeofday(&now, NULL);
    int rank = 1;  // 用于显示的排名

    // 遍历客户端信息数组
    for (size_t i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].fd != 0) { // 确保该槽位已经分配了客户端
            pthread_mutex_lock(&clients[i].lock);

            timersub(&now, &clients[i].start, &elapsed);  // 计算时间差
            double elapsed_time = elapsed.tv_sec + elapsed.tv_usec / 1000000.0;

            if (elapsed_time > 0) {  // 确保时间间隔不为0
                // 根据当前模式选择要显示的带宽
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

            // 只显示活跃的客户端
            if (clients[i].total_bytes_up > 0 || bandwidth_mbps > 0) {
                if (current_mode == MODE_UP) {
                    mvwprintw(main_win, rank + 10, 1, "| [%2d] | %s\t|  %d | %8.2f Mbps |", 
                        rank, clients[i].ip, clients[i].port, bandwidth_mbps);
                } else if (current_mode == MODE_DOWN) {
                    mvwprintw(main_win, rank + 10, 1, "| [%2d] | %s\t|  %d | DOWN\t| %8.2f Mbps |", 
                        rank, clients[i].ip, clients[i].port, bandwidth_mbps);
                } else {
                    mvwprintw(main_win, rank + 10, 1, "| [%2d] | %s\t|  %d | UP\t| %8.2f Mbps | DOWN |", 
                        rank, clients[i].ip, clients[i].port, bandwidth_mbps);
                }
                rank++;  // 只增加已显示的客户端排名
            }

            wrefresh(main_win); // 刷新窗口以显示更新的信息

            // 如果采样间隔太小，保留数据量而不重置
            if (elapsed_time >= 1.0) {
                clients[i].total_bytes_up = 0;
                clients[i].total_bytes_down = 0;
                gettimeofday(&clients[i].start, NULL);  // 重置起始时间
            }

            pthread_mutex_unlock(&clients[i].lock);
        }
    }

    // 清除多余的行（如果有客户端断开，行数可能会减少）
    for (int j = rank; j <= MAX_CLIENTS; j++) {
        mvwprintw(main_win, j + 10, 1, "|      | \t\t|        | \t\t | \t\t |"); // 清空行内容
    }
    wrefresh(main_win);
}

void* handle_client(void* arg) {
    client_info_t* client = (client_info_t*)arg;
    char buffer[BUFFER_SIZE];
    ssize_t len;

    while ((len = recv(client->fd, buffer, BUFFER_SIZE, 0)) > 0) {
        pthread_mutex_lock(&client->lock);

        // 根据当前模式处理数据
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

int main() {
    int server_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    pthread_t threads[MAX_CLIENTS];
    int thread_count = 0;

    memset(clients, 0, sizeof(clients)); // 初始化客户端信息数组

    // 创建 TCP 套接字
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    // 配置服务器地址
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    // 绑定套接字到地址
    if (bind(server_fd, (const struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // 监听端口
    if (listen(server_fd, MAX_CLIENTS) < 0) {
        perror("listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // 初始化ncurses
    initscr();
    cbreak();
    noecho();
    curs_set(0);
    mousemask(ALL_MOUSE_EVENTS, NULL);  // 启用鼠标事件

    main_win = newwin(MAX_CLIENTS * 2 + 4, 80, 0, 0);
    box(main_win, 0, 0);
    mvwprintw(main_win, 1, 1, "Server listening on port %d...", SERVER_PORT);
    mvwprintw(main_win, 1, 1, "Server    addr: \t192.168.18.125\t\tport: 8888");
    mvwprintw(main_win, 2, 1, "Broadcast addr: \t192.168.18.255\t\tport: 5005"); // 广播地址 为实现广播功能
    mvwprintw(main_win, 3, 1, "Test      time: \t500 sec        \t\tPakg size: 1470 bit");
    mvwprintw(main_win, 4, 1, "Mode:    %s \t\t\t\tLimit: no limit", get_mode_string(current_mode));
    mvwprintw(main_win, 6, 1, "pre        next \tset-limit\t\texit(press 'q')");
    mvwprintw(main_win, 8, 1, "| RANK | IP\t\t|  PORT  | UP\t\t | DOWN\t\t |");
    mvwprintw(main_win, 9, 1, "-----------------------------------------------------------------");
    wrefresh(main_win);

    struct itimerval timer;
    signal(SIGALRM, handle_alarm);

    timer.it_value.tv_sec = 1;
    timer.it_value.tv_usec = 0;
    timer.it_interval.tv_sec = 1;
    timer.it_interval.tv_usec = 0;

    setitimer(ITIMER_REAL, &timer, NULL);

    int ch;
    MEVENT event;
    while ((ch = wgetch(main_win)) != 'q') {
        switch (ch) {
            case KEY_MOUSE:
                if (getmouse(&event) == OK) {
                    // 检查鼠标点击的位置
                    if (event.y == 4 && event.x >= 7 && event.x <= 13) { // 假设模式选项在第4行，第7到第13列之间
                        switch (current_mode) {
                            case MODE_DOUBLE: current_mode = MODE_UP; break;
                            case MODE_UP: current_mode = MODE_DOWN; break;
                            case MODE_DOWN: current_mode = MODE_DOUBLE; break;
                        }
                        mvwprintw(main_win, 4, 1, "Mode:    %s \t\t\t\tLimit: no limit", get_mode_string(current_mode));
                        wrefresh(main_win);
                    }
                }
                break;
        }
    }

    // 关闭套接字并清理资源
    close(server_fd);
    delwin(main_win);
    endwin();

    return 0;
}
