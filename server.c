#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <signal.h>
#include <ncurses.h>
#include <netdb.h> // getaddrinfo, freeaddrinfo
#include <sys/ioctl.h>
#include <net/if.h>

#define SERVER_PORT 5201
#define BUFFER_SIZE 1470
#define MAX_CLIENTS 10

WINDOW *main_win; // ncurses窗口指针

// 记录每个客户端的状态
typedef struct {
    int fd;                    // 客户端套接字
    long total_bytes_up;        // 累计上传字节数
    long total_bytes_down;      // 累计下载字节数
    struct timeval start;       // 统计开始时间
    pthread_mutex_t lock;       // 锁用于线程安全
    char ip[INET_ADDRSTRLEN];   // 客户端IP地址
    int port;                   // 客户端端口
    struct sockaddr_in client_addr;  // 客户端UDP地址
    int is_active; // 判断当前客户端是否活跃 用于计数
    struct timeval last_active_time; // 新增字段，记录最后活跃时间 用于心跳包
} client_info_t;

client_info_t clients[MAX_CLIENTS];
int is_tcp = 1; // 默认TCP
char server_ip[INET_ADDRSTRLEN] = "127.0.0.1";
// int current_mode = 0;
double bandwidth_limit_mbps = 0.0;
pthread_mutex_t bandwidth_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t client_count_lock = PTHREAD_MUTEX_INITIALIZER;
int connected_clients = 0;
int run_time = 0;
int mode = 0; // 0 UP 1 DOWN 3 DOUBLE

// 计算带宽并在ncurses窗口中显示
void handle_alarm(int sig) {
    double up_bandwidth_mbps, down_bandwidth_mbps;
    struct timeval now, elapsed;

    gettimeofday(&now, NULL);
    int rank = 1;  // 用于显示的排名

    // 显示当前带宽限制
    mvwprintw(main_win, 6, 1, "Current Bandwidth Limit: %.2f Mbps", bandwidth_limit_mbps);
    wrefresh(main_win);

    // 遍历客户端信息数组
    for (size_t i = 0; i < MAX_CLIENTS; i++) {
        // if (clients[i].fd != 0) { // 确保该槽位已经分配了客户端
        if (clients[i].is_active) {
            pthread_mutex_lock(&clients[i].lock);

            timersub(&now, &clients[i].start, &elapsed);  // 计算时间差
            double elapsed_time = elapsed.tv_sec + elapsed.tv_usec / 1000000.0;

            if (elapsed_time > 0) {  // 确保时间间隔不为0
                up_bandwidth_mbps = (clients[i].total_bytes_up * 8.0) / elapsed_time / 1e6;
                down_bandwidth_mbps = (clients[i].total_bytes_down * 8.0) / elapsed_time / 1e6;
            } else {
                up_bandwidth_mbps = 0;
                down_bandwidth_mbps = 0;
            }

            // 显示客户端的上传和下载带宽
            if (clients[i].total_bytes_up > 0 || clients[i].total_bytes_down > 0) {
                mvwprintw(main_win, rank + 10, 1, "| [%2d] | %s |  %d | %8.2f Mbps | %8.2f Mbps |",
                    rank, clients[i].ip, clients[i].port, up_bandwidth_mbps, down_bandwidth_mbps);
                rank++;
            }

            wrefresh(main_win); // 刷新窗口以显示更新的信息

            // 重置上传和下载字节数以及起始时间，避免带宽重复累计
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
        mvwprintw(main_win, j + 10, 1, "|      | \t\t |        | \t\t  | \t\t  |"); // 清空行内容
    }
    wrefresh(main_win);
}

// 将输入的limit值发给客户端 
void send_bandwidth_limit_to_clients(double new_limit) {
    char limit_message[50];
    snprintf(limit_message, sizeof(limit_message), "BANDWIDTH_LIMIT:%.2f", new_limit);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].is_active) {
            if (is_tcp) {
                send(clients[i].fd, limit_message, strlen(limit_message), 0);
            } else {
                sendto(clients[i].fd, limit_message, strlen(limit_message), 0, (struct sockaddr*)&clients[i].client_addr, sizeof(clients[i].client_addr));
            }
        }
    }
}

void* listen_for_input(void* arg) {
    char input[10];
    while (1) {
        mvwprintw(main_win, 7, 1, "Enter new bandwidth limit (Mbps): ");
        wrefresh(main_win);

        echo();
        wgetnstr(main_win, input, 8); // 字符串输入
        noecho();

        double new_limit = atof(input);

        if (new_limit > 0) {
            pthread_mutex_lock(&bandwidth_lock);
            bandwidth_limit_mbps = new_limit;
            pthread_mutex_unlock(&bandwidth_lock);

            send_bandwidth_limit_to_clients(new_limit);

            mvwprintw(main_win, 8, 1, "bandwidth limit updated to %.2f Mbps", bandwidth_limit_mbps);
            wrefresh(main_win);
        } else {
            mvwprintw(main_win, 8, 1, "invalid input. please enter a positive number.");
            wrefresh(main_win);
        }

        sleep(1);
        mvwprintw(main_win, 8, 1, "                                                      ");
        wrefresh(main_win);
    }
}

// 处理TCP客户端上传数据
void* handle_tcp_client_upload(void* arg) {
    client_info_t* client = (client_info_t*)arg;
    char buffer[BUFFER_SIZE];
    ssize_t len;

    // TCP 模式上传处理
    while ((len = recv(client->fd, buffer, BUFFER_SIZE, 0)) > 0) {
        pthread_mutex_lock(&client->lock);
        client->total_bytes_up += len;  // 记录上传的字节数
        pthread_mutex_unlock(&client->lock);
    }

    close(client->fd);

    // 设备数减一 仅仅在设备活跃时执行
    pthread_mutex_lock(&client_count_lock);
    if (client->is_active) {
        connected_clients --;
        client->is_active = 0;
    }
    pthread_mutex_unlock(&client_count_lock);
    // 更新界面
    mvwprintw(main_win, 3, 1, "connected_clients: \t%d\t\t Running   time:     %d", connected_clients, run_time);
    wrefresh(main_win);

    pthread_exit(NULL);
}

// 处理TCP客户端下载数据
void* handle_tcp_client_download(void* arg) {
    client_info_t* client = (client_info_t*)arg;
    char buffer[BUFFER_SIZE];
    memset(buffer, 'D', BUFFER_SIZE);  // 模拟下载数据
    ssize_t len;

    while (1) {
        len = send(client->fd, buffer, BUFFER_SIZE, 0);  // 向客户端发送数据
        if (len <= 0) {
            break;
        }

        pthread_mutex_lock(&client->lock);
        client->total_bytes_down += len;  // 记录下载的字节数
        pthread_mutex_unlock(&client->lock);
    }

    close(client->fd);

    // 设备数减一
    pthread_mutex_lock(&client_count_lock);
    if (client->is_active) {
        connected_clients --;
        client->is_active = 0;
    }
    pthread_mutex_unlock(&client_count_lock);
    // 更新界面
    mvwprintw(main_win, 3, 1, "connected_clients: \t%d\t\t Running   time:     %d", connected_clients, run_time);
    wrefresh(main_win);

    pthread_exit(NULL);
}

// 处理UDP客户端上传和下载数据
void* handle_udp_clients(void* arg) {
    int server_fd = *(int*)arg;
    char buffer[BUFFER_SIZE];
    char buffer_response[BUFFER_SIZE];
    ssize_t len;
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);

    while (1) {
        len = recvfrom(server_fd, buffer, BUFFER_SIZE, 0, (struct sockaddr*)&client_addr, &addr_len);
        if (len < 0) {
            perror("recvfrom failed");
            continue;
        }

        client_info_t* client = NULL;
        for (size_t i = 0; i < MAX_CLIENTS; i ++) {
            if (clients[i].is_active && memcmp(&clients[i].client_addr, &client_addr, sizeof(client_addr)) == 0 && clients[i].client_addr.sin_port == client_addr.sin_port) {
                client = &clients[i];
                break;
            }
        }

        if (client == NULL) { // 寻找空闲的客户端槽
            for (size_t i = 0; i < MAX_CLIENTS; i ++) {
                if (!clients[i].is_active) {
                    client = &clients[i];
                    client->total_bytes_up = 0;
                    client->total_bytes_down = 0;
                    client->is_active = 1;
                    client->client_addr = client_addr;

                    gettimeofday(&client->start, NULL);
                    pthread_mutex_init(&client->lock, NULL);

                    inet_ntop(AF_INET, &(client_addr.sin_addr), client->ip, INET_ADDRSTRLEN);
                    client->port = ntohs(client_addr.sin_port);

                    pthread_mutex_lock(&client_count_lock);
                    connected_clients ++;
                    pthread_mutex_unlock(&client_count_lock);

                    mvwprintw(main_win, 3, 1, "connected_clients: \t%d\t\t UdpRunningtime:     %d", connected_clients, run_time);
                    wrefresh(main_win);

                    // 发送确认包给客户端
                    char ack_msg[] = "ACK";
                    sendto(server_fd, ack_msg, sizeof(ack_msg), 0, (struct sockaddr*)&client->client_addr, addr_len);

                    break;
                }
            }
        }

        gettimeofday(&client->last_active_time, NULL); // 如果客户端存在 更新最后活跃时间

        if (client == NULL) {
            printf("Max clients reached. Connection refused.\n");
            continue;
        }

        if (mode == 0 || mode == 2) {
            pthread_mutex_lock(&client->lock);
            client->total_bytes_up += len;
            pthread_mutex_unlock(&client->lock);
        }
        if (mode == 1 || mode == 2) {
            memset(buffer_response, 'S', BUFFER_SIZE);

            len = sendto(server_fd, buffer_response, BUFFER_SIZE, 0, (struct sockaddr*)&client->client_addr, addr_len);
            if (len < 0) {
                perror("sendto failed");
                break;
            }
            pthread_mutex_lock(&client->lock);
            client->total_bytes_down += len;
            pthread_mutex_unlock(&client->lock);

        }
    }
}

void* monitor_clients(void* arg) {
    while (1) {
        for (size_t i = 0; i < MAX_CLIENTS; i ++) {
            if (clients[i].is_active) {
                pthread_mutex_lock(&clients[i].lock);

                if (clients[i].total_bytes_up == 0 && clients[i].total_bytes_down == 0) {
                    clients[i].is_active = 0;

                    pthread_mutex_lock(&client_count_lock);
                    connected_clients --;
                    pthread_mutex_unlock(&client_count_lock);

                    mvwprintw(main_win, 3, 1, "connected_clients: \t%d\t\t UdpRunningtime:     %d", connected_clients, run_time);
                    wrefresh(main_win);
                }

                clients[i].total_bytes_up = 0;
                clients[i].total_bytes_down = 0;

                pthread_mutex_unlock(&clients[i].lock);
            }
        }

        sleep(1);
    }
}

// 获取指定网络接口 ip地址
int get_interface_ip(const char *interface, char *ip_buffer, size_t buffer_size) {
    int fd;
    struct ifreq ifr;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("socket failed");
        return -1;
    }

    strncpy(ifr.ifr_name, interface, IFNAMSIZ - 1);

    // 通过 ioctl 获取网络接口信息
    if (ioctl(fd, SIOCGIFADDR, &ifr) < 0) {
        perror("ioctl failed");
        close(fd);
        return -1;
    }

    // 获取IP地址
    struct sockaddr_in* ipaddr = (struct sockaddr_in*)&ifr.ifr_addr;
    inet_ntop(AF_INET, &ipaddr->sin_addr, ip_buffer, buffer_size);

    close(fd);
    return 0;
}

// 处理客户端广播请求 发送服务器IP地址
void* handle_broadcast_requests(void* arg) {
    int server_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);
    char buffer[BUFFER_SIZE];
    char server_ip[INET_ADDRSTRLEN];

    if ((server_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("udp socket create failed");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(5202);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // 获取 有线网络接口 ip地址
    if (get_interface_ip("enp2s0", server_ip, sizeof(server_ip)) != 0) {
        fprintf(stderr, "Failed to get IP address for enp2s0\n");
        close(server_fd);
        return NULL;
    }

    mvwprintw(main_win, 1, 1, "Server    IP:\t\t%s\t Server    Port:     %d", server_ip, SERVER_PORT);
    wrefresh(main_win);

    while (1) { // 接收客户端请求
        ssize_t len = recvfrom(server_fd, buffer, BUFFER_SIZE, 0, (struct sockaddr*)&client_addr, &addr_len);
        if (len > 0) {
            sendto(server_fd, server_ip, strlen(server_ip), 0, (struct sockaddr*)&client_addr, addr_len);
        }
    }

    close(server_fd);
    return NULL;
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <tcp/udp> <up/down/double>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    if (strcmp(argv[1], "tcp") == 0) {
        is_tcp = 1; // TCP模式
    } else if (strcmp(argv[1], "udp") == 0) {
        is_tcp = 0; // UDP模式
    } else {
        fprintf(stderr, "Invalid protocol: %s. Use 'tcp' or 'udp'.\n", argv[1]);
        exit(EXIT_FAILURE);
    }

    if (strcmp(argv[2], "up") == 0) {
        mode = 0; // UP
    } else if (strcmp(argv[2], "down") == 0) {
        mode = 1; // DOWN
    } else if (strcmp(argv[2], "double") == 0) {
        mode = 2; // DOUBLE
    } else {
        fprintf(stderr, "Invalid mode: %s. Use 'up', 'down', or 'double'.\n", argv[2]);
        exit(EXIT_FAILURE);
    }

    // 初始化ncurses
    initscr();
    cbreak();
    noecho(); // 不回显
    curs_set(0);
    main_win = newwin(MAX_CLIENTS * 2 + 4, 80, 0, 0);
    box(main_win, 0, 0);
    mvwprintw(main_win, 1, 1, "Server    IP:\t\t%s\t Server    Port:     %d", server_ip, SERVER_PORT);
    mvwprintw(main_win, 2, 1, "Broadcast IP:\t\t%s\t Broadcast Port:     %d", "192.168.18.255", 5202);
    mvwprintw(main_win, 3, 1, "connected_clients: \t%d\t\t Running   time:     %d", connected_clients, run_time);
    mvwprintw(main_win, 4, 1, "Current Mode: \t%s", mode == 0 ? "UP" : (mode == 1) ? "DOWN" : "DOUBLE");
    // mvwprintw(main_win, 5, 1, "Bandwidth Limit: %.2f Mbps", bandwidth_limit_mbps); // 在 listen_for_input 中实现

    // 6 7 8 lines handle limit input
    
    mvwprintw(main_win, 9, 1, "| RANK | IP\t\t |  PORT  | UP\t\t  | DOWN\t  |");
    mvwprintw(main_win,10, 1, "-----------------------------------------------------------------");
    wrefresh(main_win);

    // 设置定时器，每秒触发一次
    struct itimerval timer;
    signal(SIGALRM, handle_alarm);
    timer.it_value.tv_sec = 1;
    timer.it_value.tv_usec = 0;
    timer.it_interval.tv_sec = 1;
    timer.it_interval.tv_usec = 0;
    setitimer(ITIMER_REAL, &timer, NULL);

    // 监听客户端是否活跃
    pthread_t monitor_thread;
    if (pthread_create(&monitor_thread, NULL, monitor_clients, NULL) != 0) {
        perror("failed to create monitor_thread");
        exit(EXIT_FAILURE);
    }

    // 监听处理广播请求
    pthread_t broadcast_thread;
    if (pthread_create(&broadcast_thread, NULL, handle_broadcast_requests, NULL) != 0) {
        perror("failed to create broadcast_thread");
        exit(EXIT_FAILURE);
    }

    // 监听带宽限制输入
    pthread_t input_thread;
    if (pthread_create(&input_thread, NULL, listen_for_input, NULL) != 0) {
        perror("failed to create input thread");
        exit(EXIT_FAILURE);
    }

    int server_fd;
    struct sockaddr_in server_addr;
    // struct sockaddr_in server_addr, client_addr;
    // socklen_t client_addr_len = sizeof(client_addr);
    pthread_t threads[MAX_CLIENTS * 2];
    int thread_count = 0;

    pthread_t udp_thread;

    memset(clients, 0, sizeof(clients)); // 初始化客户端信息数组

    // 创建 TCP 或 UDP 套接字
    if (is_tcp) {
        if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
            perror("socket creation failed");
            exit(EXIT_FAILURE);
        }
    } else {
        if ((server_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
            perror("UDP socket creation failed");
            exit(EXIT_FAILURE);
        }
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

    if (is_tcp) {
        // TCP模式下监听端口
        if (listen(server_fd, MAX_CLIENTS) < 0) {
            perror("listen failed");
            close(server_fd);
            exit(EXIT_FAILURE);
        }

        while (1) {
            int* client_fd = malloc(sizeof(int));
            struct sockaddr_in client_addr;
            socklen_t client_addr_len = sizeof(client_addr);

            if (client_fd == NULL) {
                perror("malloc failed");
                close(server_fd);
                endwin();
                exit(EXIT_FAILURE);
            }
            // TCP模式下接收客户端请求
            *client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_addr_len);
            if (*client_fd < 0) {
                perror("accept failed");
                free(client_fd);
                continue;
            }

            // 寻找空闲的客户端槽
            client_info_t* client = NULL;
            for (size_t i = 0; i < MAX_CLIENTS; i++) {
                if (clients[i].fd == 0) {
                    client = &clients[i];
                    client->fd = *client_fd;
                    client->total_bytes_up = 0;
                    client->total_bytes_down = 0;
                    client->is_active = 1;

                    gettimeofday(&client->start, NULL); // 获取当前时间
                    pthread_mutex_init(&client->lock, NULL);

                    // 记录 IP 和端口
                    inet_ntop(AF_INET, &(client_addr.sin_addr), client->ip, INET_ADDRSTRLEN);
                    client->port = ntohs(client_addr.sin_port);
                    client->client_addr = client_addr; // UDP模式下需要存储客户端地址

                    pthread_mutex_lock(&client_count_lock);
                    connected_clients ++;
                    pthread_mutex_unlock(&client_count_lock);
                    // 更新界面
                    mvwprintw(main_win, 3, 1, "connected_clients: \t%d\t\t Running   time:     %d", connected_clients, run_time);
                    wrefresh(main_win);

                    break;
                }
            }

            free(client_fd);

            if (client != NULL) {
                // 根据选择的模式，创建相应的线程
                if (mode == 0 || mode == 2) {  // UP 模式或 DOUBLE 模式都启动上传线程
                    if (pthread_create(&threads[thread_count++], NULL, handle_tcp_client_upload, client) != 0) {
                        perror("pthread_create for upload failed");
                    }
                }

                if (mode == 1 || mode == 2) {  // DOWN 模式或 DOUBLE 模式都启动下载线程
                    if (pthread_create(&threads[thread_count++], NULL, handle_tcp_client_download, client) != 0) {
                        perror("pthread_create for download failed");
                    }
                }

                // 防止线程数量超过上限
                if (thread_count >= MAX_CLIENTS * 2) {
                    thread_count = 0;
                    for (int i = 0; i < MAX_CLIENTS * 2; i++) {
                        pthread_join(threads[i], NULL);
                    }
                }
            } else {
                printf("Max clients reached. Connection refused.\n");
            }
        }
    } else {
        // UDP test
        if (pthread_create(&udp_thread, NULL, handle_udp_clients, &server_fd) != 0) {
            perror("Failed to create udp_thread");
            exit(EXIT_FAILURE);
        }
        pthread_join(udp_thread, NULL);
    }

    close(server_fd);
    pthread_join(monitor_thread, NULL);
    pthread_join(broadcast_thread, NULL);
    endwin();
    return 0;
}
