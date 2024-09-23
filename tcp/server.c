#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <sys/time.h>
#include <signal.h>
#define _GNU_SOURCE
#include <sys/epoll.h>

#include <ncurses.h>
#include <locale.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <bits/sigaction.h>
#include <asm-generic/signal-defs.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <unistd.h>
#include <netinet/tcp.h>

#define MAX_EVENTS 12
#define NUM_THREADS 4
#define BUFFER_SIZE 2048
#define MAX_CLIENTS 13
#define PORT 8888
#define BROADCAST_IP "192.168.1.255" // 目标广播地址
#define BROADCAST_PORT 8888          // 目标端口
#define BROADCAST_INTERVAL 1         // 每秒发送一次
#define BROADCAST_DURATION 2        // 发送持续10秒

#define SERVER_PORT 9999 // 监听的端口
// #define MAX_EVENTS 10    // 最大事件数
// #define BUFFER_SIZE 1024 // 缓冲区大小

// 函数声明
int setup_epoll_server(int port);
void handle_client_connections_up(int listen_fd);
void handle_client_connections_down(int listen_fd);
void handle_client_connections_double(int listen_fd);

// 全局变量
volatile int global_var = 0;   // 全局变量，用于被监视的值
volatile int global_var_1 = 0; // 全局变量，用于被监视的值
char input[100] = " ";         // 输入缓冲区' ';
int input_1 = 0;               // 限速大小

int listen_fd, epollfd;
struct sockaddr_in server_addr;
struct epoll_event ev, events[MAX_EVENTS];
char client_ip[INET_ADDRSTRLEN];
int ret;
socklen_t addr_len = sizeof(server_addr);
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond_var = PTHREAD_COND_INITIALIZER;
int current_task = 0; // 0: no task, 1: upload, 2: download, 3: double
pthread_t upload_thread, download_thread, double_thread;

typedef struct
{
    int client_fd;                   // 客户端文件描述符
    char client_ip[INET_ADDRSTRLEN]; // 客户端IP地址
    int port;                        // 客户端port端口
    long int sum;
    long int sum1; // 数据累计值
} client_info_t;

WINDOW *win;
client_info_t clients[MAX_CLIENTS];
int height, width, start_y, start_x;
int client_fds[FD_SETSIZE];
int client_count = 0;

void cleanup(void *arg)
{
    // 进行资源清理
    // 停止定时器
    struct itimerval stop_timer;
    stop_timer.it_value.tv_sec = 0;
    stop_timer.it_value.tv_usec = 0;
    stop_timer.it_interval.tv_sec = 0;
    stop_timer.it_interval.tv_usec = 0;
    setitimer(ITIMER_REAL, &stop_timer, NULL);

    // printf("Cleaning up resources\n");
}

void handle_alarm(int signum)
{
    double bandwidth[MAX_CLIENTS] = {0};
    for (size_t i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i].port == 0)
        {
            continue;
        }
        bandwidth[i] = ((double)clients[i].sum * 8.0) / 1048576;                                                                                                         // 转换为 Mbps
        
        mvwprintw(win, i + 9, 5, "| [%2d] | %-15s|  %-5d | %8.2f Mbps |      N/A      |", i, clients[i].client_ip, clients[i].port, bandwidth[i]);
        wrefresh(win);
        clients[i].sum = 0; // 清零累计值，准备下一次统计
    }
}

void handle_alarm1(int signum)
{
    double bandwidth[MAX_CLIENTS] = {0};
    for (size_t i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i].port == 0)
        {
            continue;
        }
        bandwidth[i] = ((double)clients[i].sum1 * 8.0) / 1048576;                                                                                                         // 转换为 Mbps
        
        mvwprintw(win, i + 9, 5, "| [%2d] | %-15s|  %-5d |      N/A      | %8.2f Mbps |", i, clients[i].client_ip, clients[i].port, bandwidth[i]);
        wrefresh(win);
        clients[i].sum1 = 0;
    }
}

void handle_alarm2(int signum)
{
    double bandwidth[MAX_CLIENTS] = {0};
    double bandwidth1[MAX_CLIENTS] = {0};
    for (size_t i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i].port == 0)
        {
            continue;
        }
        bandwidth[i] = ((double)clients[i].sum * 8.0) / 1048576;
        bandwidth1[i] = ((double)clients[i].sum1 * 8.0) / 1048576;                                                                                                       // 转换为 Mbps
        // mvwprintw(win, i + 15, 1, "|IP:| %-16s | UP: | %-6.2lf Mbps |   DOWN: | %-6.2lf Mbps", clients[i].client_ip, bandwidth[i], bandwidth1[i]); // 在窗口中显示文本
        mvwprintw(win, i + 9, 5, "| [%2d] | %-15s|  %-5d | %8.2f Mbps | %8.2f Mbps |", i, clients[i].client_ip, clients[i].port, bandwidth[i], bandwidth1[i]);
        wrefresh(win);
        clients[i].sum = 0;
        clients[i].sum1 = 0; // 清零累计值，准备下一次统计
    }
}

int epollfd;

// 获取 enps20 有线网口ip
void get_local_ip(char *ip_buffer, size_t buffer_size)
{
    struct ifaddrs *ifaddr, *ifa;
    getifaddrs(&ifaddr);

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next)
    {
        if (ifa->ifa_addr == NULL)
            continue;

        // 只检查名为 "enp2s0" 的网络接口
        if (strcmp(ifa->ifa_name, "enp2s0") == 0 && ifa->ifa_addr->sa_family == AF_INET)
        {
            // 获取该接口的 IPv4 地址
            void *addr_ptr = &((struct sockaddr_in *)ifa->ifa_addr)->sin_addr;
            inet_ntop(AF_INET, addr_ptr, ip_buffer, buffer_size);

            // 确保不是回环地址（127.0.0.1）
            if (strcmp(ip_buffer, "127.0.0.1") != 0)
            {
                break;
            }
        }
    }

    freeifaddrs(ifaddr);
}
// 发送广播线程函数 
void *broadcast_ip(void *arg)
{
    int sockfd;
    struct sockaddr_in broadcast_addr;
    char local_ip[INET_ADDRSTRLEN];

    // 获取本机物理 IP 地址
    get_local_ip(local_ip, sizeof(local_ip));
    // printf("本机IP地址: %s\n", local_ip);

    // 创建UDP套接字
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
    {
        perror("broadcast create failed");
        return NULL;
    }

    // 设置套接字为广播模式
    int broadcast_enable = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, &broadcast_enable, sizeof(broadcast_enable)) < 0)
    {
        perror("set broadcast failed");
        close(sockfd);
        return NULL;
    }

    // 配置广播地址
    memset(&broadcast_addr, 0, sizeof(broadcast_addr));
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_port = htons(PORT);
    broadcast_addr.sin_addr.s_addr = inet_addr(BROADCAST_IP);

    // 开始循环广播10秒，每秒发送一次本机IP地址
    for (int i = 0; ; i++)
    {
        if (sendto(sockfd, local_ip, strlen(local_ip), 0, (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr)) < 0)
        {
            perror("send broadcast message failed");
        }
        else
        {
            // printf("已发送广播消息: %s\n", local_ip);
        }
        sleep(BROADCAST_INTERVAL);
    }

    close(sockfd);
    return NULL;
}

// 初始化 epoll 和服务器套接字，开始监听客户端连接
int setup_epoll_server(int port)
{
    int listen_fd;
    struct sockaddr_in server_addr;
    struct epoll_event ev;

    memset(clients, 0, sizeof(clients)); // 初始化客户端信息数组

    // 创建监听套接字
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == -1)
    {
        perror("socket");
        return -1;
    }

        // 设置地址重用选项（可选）
    int reuse = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
    {
        perror("setsockopt");
        close(listen_fd);
        return -1;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // 绑定地址和端口
    int ret = bind(listen_fd, (const struct sockaddr *)&server_addr, sizeof(server_addr));
    if (ret == -1)
    {
        perror("bind");
        close(listen_fd);
        return -1;
    }

    // 开始监听
    ret = listen(listen_fd, MAX_CLIENTS);
    if (ret == -1)
    {
        perror("listen");
        close(listen_fd);
        return -1;
    }

    // 创建 epoll 实例
    epollfd = epoll_create1(0);
    if (epollfd == -1)
    {
        perror("epoll_create1");
        close(listen_fd);
        return -1;
    }

    ev.events = EPOLLIN;
    ev.data.fd = listen_fd;

    // 将监听套接字添加到 epoll 监控列表中
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, listen_fd, &ev) == -1)
    {
        perror("epoll_ctl");
        close(listen_fd);
        return -1;
    }

    return listen_fd;
}

// 处理客户端数据
void handle_client_connections_up(int listen_fd)
{
    // 信号处理
    signal(SIGALRM, handle_alarm);
    struct itimerval timer;
    timer.it_value.tv_sec = 1; // 1秒后触发第一次
    timer.it_value.tv_usec = 0;
    timer.it_interval.tv_sec = 1; // 每隔1秒触发一次
    timer.it_interval.tv_usec = 0;
    setitimer(ITIMER_REAL, &timer, NULL);

    struct epoll_event events[MAX_EVENTS];
    struct epoll_event ev;
    char client_ip[INET_ADDRSTRLEN];

    while (1)
    {
        int nfds = epoll_wait(epollfd, events, MAX_EVENTS, -1);
        if (nfds == -1)
        {
            if (errno == EINTR)
            {
                continue;
            }
            else
            {
                perror("epoll_wait");
                close(listen_fd);
                close(epollfd);
                exit(EXIT_FAILURE);
            }
        }

        // 遍历所有就绪的文件描述符
        for (int i = 0; i < nfds; i++)
        {
            if (events[i].data.fd == listen_fd)
            {
                // 处理新客户端连接
                struct sockaddr_in client_addr;
                socklen_t client_len = sizeof(client_addr);
                int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
                if (client_fd == -1)
                {
                    perror("accept failed");
                    continue;
                }

                inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);

                // 将客户端添加到客户端信息数组
                int client_index = -1;
                for (int j = 0; j < MAX_CLIENTS; j++)
                {
                    if (clients[j].client_fd == 0)
                    {
                        client_index = j;
                        clients[j].client_fd = client_fd;
                        clients[j].port = ntohs(client_addr.sin_port); // 记录客户端端口
                        strncpy(clients[j].client_ip, client_ip, INET_ADDRSTRLEN);
                        break;
                    }
                }

                if (client_index != -1)
                {
                    ev.events = EPOLLIN;
                    ev.data.fd = client_fd;
                    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, client_fd, &ev) == -1)
                    {
                        perror("epoll_ctl: client_fd");
                        close(client_fd);
                    }
                }
                else
                {
                    printf("client max\n");
                    close(client_fd);
                }
            }
            else if (events[i].events & EPOLLIN)
            {
                // 处理已连接客户端的数据
                int client_fd = events[i].data.fd;

                char buffer[BUFFER_SIZE];
                ssize_t len = recv(client_fd, buffer, BUFFER_SIZE, 0);
                if (len <= 0)
                {
                    if (len == 0)
                    {
                        // 客户端断开连接
                        // printf("客户端断开连接: fd %d\n", client_fd);
                    }
                    else
                    {
                        perror("recv failed");
                    }
                    close(client_fd);
                    epoll_ctl(epollfd, EPOLL_CTL_DEL, client_fd, NULL);

                    // 移除客户端信息
                    for (int j = 0; j < MAX_CLIENTS; j++)
                    {
                        if (clients[j].client_fd == client_fd)
                        {
                            clients[j].client_fd = 0;
                            break;
                        }
                    }
                }
                else
                {
                    // 记录客户端接收到的数据长度
                    for (int j = 0; j < MAX_CLIENTS; j++)
                    {
                        if (clients[j].client_fd == client_fd)
                        {
                            clients[j].sum += len;
                            break;
                        }
                    }
                }
            }
        }
    }
}

void handle_client_connections_down(int listen_fd)
{

    // 停止定时器
    struct itimerval stop_timer;
    stop_timer.it_value.tv_sec = 0;
    stop_timer.it_value.tv_usec = 0;
    stop_timer.it_interval.tv_sec = 0;
    stop_timer.it_interval.tv_usec = 0;
    setitimer(ITIMER_REAL, &stop_timer, NULL);

    // 信号处理

    signal(SIGALRM, handle_alarm1);
    struct itimerval timer;
    timer.it_value.tv_sec = 1; // 1秒后触发第一次
    timer.it_value.tv_usec = 0;
    timer.it_interval.tv_sec = 1; // 每隔1秒触发一次
    timer.it_interval.tv_usec = 0;
    setitimer(ITIMER_REAL, &timer, NULL);

    struct epoll_event events[MAX_EVENTS];
    struct epoll_event ev;
    char client_ip[INET_ADDRSTRLEN];
    char message[BUFFER_SIZE]; // 创建包含A的缓冲区

    size_t message_len = sizeof(message); // 消息长度等于缓冲区大小

    while (1)
    {
        int nfds = epoll_wait(epollfd, events, MAX_EVENTS, -1);
        if (nfds == -1)
        {
            if (errno == EINTR)
            {
                continue;
            }
            else
            {
                perror("epoll_wait");
                close(listen_fd);
                close(epollfd);
                exit(EXIT_FAILURE);
            }
        }

        // 遍历所有就绪的文件描述符
        for (int i = 0; i < nfds; i++)
        {
            if (events[i].data.fd == listen_fd)
            {
                // 处理新客户端连接
                struct sockaddr_in client_addr;
                socklen_t client_len = sizeof(client_addr);
                int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
                if (client_fd == -1)
                {
                    perror("accept failed");
                    continue;
                }

                inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);

                // 将客户端添加到客户端信息数组
                int client_index = -1;
                for (int j = 0; j < MAX_CLIENTS; j++)
                {
                    if (clients[j].client_fd == 0)
                    {
                        client_index = j;
                        clients[j].client_fd = client_fd;
                        clients[j].port = ntohs(client_addr.sin_port); // 记录客户端端口
                        strncpy(clients[j].client_ip, client_ip, INET_ADDRSTRLEN);
                        clients[j].sum1 = 0; // 初始化发送字节数
                        break;
                    }
                }

                if (client_index != -1)
                {
                    ev.events = EPOLLOUT; // 监视可写事件
                    ev.data.fd = client_fd;
                    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, client_fd, &ev) == -1)
                    {
                        perror("epoll_ctl: client_fd");
                        close(client_fd);
                    }
                }
                else
                {
                    printf("client max\n");
                    close(client_fd);
                }
            }
            else if (events[i].events & EPOLLOUT)
            {
                // 处理可写的客户端
                int client_fd = events[i].data.fd;
                memset(message, 'A', sizeof(message)); // 用字符'A'填充缓冲区
                // 向客户端发送填充字符'A'的数据
                ssize_t sent_len = send(client_fd, message, message_len, 0);
                // if (sent_len == -1)
                // {
                //     perror("send failed");
                //     close(client_fd);
                //     epoll_ctl(epollfd, EPOLL_CTL_DEL, client_fd, NULL);

                //     // 移除客户端信息
                //     for (int j = 0; j < MAX_CLIENTS; j++)
                //     {
                //         if (clients[j].client_fd == client_fd)
                //         {
                //             clients[j].client_fd = 0;
                //             break;
                //         }
                //     }
                // }
                // else
                // {
                // 记录客户端发送的数据长度
                for (int j = 0; j < MAX_CLIENTS; j++)
                {
                    if (clients[j].client_fd == client_fd)
                    {
                        clients[j].sum1 += sent_len;
                        // printf("已发送数据：%zd 字节，客户端 fd: %d，总计发送：%ld 字节\n", sent_len, client_fd, clients[j].sum);
                        break;
                    }
                }
                // }
            }
        }
    }
}

void handle_client_connections_double(int listen_fd)
{
    // 停止定时器
    struct itimerval stop_timer;
    stop_timer.it_value.tv_sec = 0;
    stop_timer.it_value.tv_usec = 0;
    stop_timer.it_interval.tv_sec = 0;
    stop_timer.it_interval.tv_usec = 0;
    setitimer(ITIMER_REAL, &stop_timer, NULL);

    // 信号处理
    signal(SIGALRM, handle_alarm2);
    struct itimerval timer;
    timer.it_value.tv_sec = 1; // 1秒后触发第一次
    timer.it_value.tv_usec = 0;
    timer.it_interval.tv_sec = 1; // 每隔1秒触发一次
    timer.it_interval.tv_usec = 0;
    setitimer(ITIMER_REAL, &timer, NULL);

    struct epoll_event events[MAX_EVENTS];
    struct epoll_event ev;
    char client_ip[INET_ADDRSTRLEN];
    char message[BUFFER_SIZE];             // 创建包含A的缓冲区
    memset(message, 'A', sizeof(message)); // 用字符'A'填充缓冲区
    size_t message_len = sizeof(message);  // 消息长度等于缓冲区大小

    while (1)
    {
        int nfds = epoll_wait(epollfd, events, MAX_EVENTS, -1);
        if (nfds == -1)
        {
            if (errno == EINTR)
            {
                continue;
            }
            else
            {
                perror("epoll_wait");
                close(listen_fd);
                close(epollfd);
                exit(EXIT_FAILURE);
            }
        }

        // 遍历所有就绪的文件描述符
        for (int i = 0; i < nfds; i++)
        {
            if (events[i].data.fd == listen_fd)
            {
                // 处理新客户端连接
                struct sockaddr_in client_addr;
                socklen_t client_len = sizeof(client_addr);
                int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
                if (client_fd == -1)
                {
                    perror("accept failed");
                    continue;
                }

                inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);

                // 将客户端添加到客户端信息数组
                int client_index = -1;
                for (int j = 0; j < MAX_CLIENTS; j++)
                {
                    if (clients[j].client_fd == 0)
                    {
                        client_index = j;
                        clients[j].client_fd = client_fd;
                        clients[j].port = ntohs(client_addr.sin_port); // 记录客户端端口
                        strncpy(clients[j].client_ip, client_ip, INET_ADDRSTRLEN);
                        clients[j].sum = 0;  // 初始化接收字节数
                        clients[j].sum1 = 0; // 初始化发送字节数
                        break;
                    }
                }

                if (client_index != -1)
                {
                    ev.events = EPOLLIN | EPOLLOUT; // 同时监视可读和可写事件
                    ev.data.fd = client_fd;
                    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, client_fd, &ev) == -1)
                    {
                        perror("epoll_ctl: client_fd");
                        close(client_fd);
                    }
                }
                else
                {
                    printf("client max\n");
                    close(client_fd);
                }
            }
            else
            {
                int client_fd = events[i].data.fd;

                // 处理可读事件
                if (events[i].events & EPOLLIN)
                {
                    char recv_buffer[BUFFER_SIZE];
                    ssize_t recv_len = recv(client_fd, recv_buffer, sizeof(recv_buffer), 0);
                    if (recv_len == -1)
                    {
                        perror("recv failed");
                        close(client_fd);
                        epoll_ctl(epollfd, EPOLL_CTL_DEL, client_fd, NULL);

                        // 移除客户端信息
                        for (int j = 0; j < MAX_CLIENTS; j++)
                        {
                            if (clients[j].client_fd == client_fd)
                            {
                                clients[j].client_fd = 0;
                                break;
                            }
                        }
                    }
                    else if (recv_len == 0)
                    {
                        // 客户端关闭连接
                        // printf("客户端 fd: %d 关闭连接\n", client_fd);
                        close(client_fd);
                        epoll_ctl(epollfd, EPOLL_CTL_DEL, client_fd, NULL);

                        // 移除客户端信息
                        for (int j = 0; j < MAX_CLIENTS; j++)
                        {
                            if (clients[j].client_fd == client_fd)
                            {
                                clients[j].client_fd = 0;
                                break;
                            }
                        }
                    }
                    else
                    {
                        // 记录客户端接收的数据长度
                        for (int j = 0; j < MAX_CLIENTS; j++)
                        {
                            if (clients[j].client_fd == client_fd)
                            {
                                clients[j].sum += recv_len;
                                // 打印接收到的数据大小
                                // printf("接收到 %zd 字节，客户端 fd: %d，总计接收：%ld 字节\n", recv_len, client_fd, clients[j].sum);
                                break;
                            }
                        }
                    }
                }

                // 处理可写事件
                if (events[i].events & EPOLLOUT)
                {
                    ssize_t sent_len = send(client_fd, message, message_len, 0);
                    if (sent_len == -1)
                    {
                        // perror("send failed"); // 连接被对方重设
                        close(client_fd);
                        epoll_ctl(epollfd, EPOLL_CTL_DEL, client_fd, NULL);

                        // 移除客户端信息
                        for (int j = 0; j < MAX_CLIENTS; j++)
                        {
                            if (clients[j].client_fd == client_fd)
                            {
                                clients[j].client_fd = 0;
                                break;
                            }
                        }
                    }
                    else
                    {
                        // 记录客户端发送的数据长度
                        for (int j = 0; j < MAX_CLIENTS; j++)
                        {
                            if (clients[j].client_fd == client_fd)
                            {
                                clients[j].sum1 += sent_len;
                                // printf("发送数据：%zd 字节，客户端 fd: %d，总计发送：%ld 字节\n", sent_len, client_fd, clients[j].sum1);
                                break;
                            }
                        }
                    }
                }
            }
        }
    }
}

// 上传线程函数
void *upload_thread_func(void *arg)
{
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    // mvwprintw(win, 25, 1, "上传线程启动...\n");
    pthread_cleanup_push(cleanup, NULL);
    handle_client_connections_up(listen_fd); // 执行上传任务
    pthread_cleanup_pop(1);
    // printf("上传任务完成，线程终止...\n");
    return NULL;
}

// 下载线程函数
void *download_thread_func(void *arg)
{
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    // mvwprintw(win, 25, 1, "下载线程启动...\n");
    pthread_cleanup_push(cleanup, NULL);
    handle_client_connections_down(listen_fd); // 执行下载任务
    pthread_cleanup_pop(1);
    // printf("下载任务完成，线程终止...\n");
    return NULL;
}

// 上传+下载线程函数
void *double_thread_func(void *arg)
{
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    // mvwprintw(win, 25, 1, "上传+下载线程启动...\n");
    pthread_cleanup_push(cleanup, NULL);
    handle_client_connections_double(listen_fd); // 执行上传+下载任务
    pthread_cleanup_pop(1);
    // printf("上传+下载任务完成，线程终止...\n");
    return NULL;
}

// 取消当前执行的线程并创建新的任务线程
void create_new_task(int task)
{
    if (current_task == 1 && pthread_cancel(upload_thread) == 0)
    {
        mvwprintw(win, 24, 1, "cancel up thread.......\n");
    }
    else if (current_task == 2 && pthread_cancel(download_thread) == 0)
    {
        mvwprintw(win, 24, 1, "cancel down thread.......\n");
    }
    else if (current_task == 3 && pthread_cancel(double_thread) == 0)
    {
        mvwprintw(win, 24, 1, "cancel double thread...\n");
    }

    current_task = task; // 更新当前任务

    // 根据新任务创建对应的线程
    if (task == 1)
    {
        pthread_create(&upload_thread, NULL, upload_thread_func, NULL);
    }
    else if (task == 2)
    {
        pthread_create(&download_thread, NULL, download_thread_func, NULL);
    }
    else if (task == 3)
    {
        pthread_create(&double_thread, NULL, double_thread_func, NULL);
    }
}

void ncurses_main()
{
    int height, width, start_y, start_x;
    struct ifaddrs *ifaddr, *ifa;
    char ip[INET_ADDRSTRLEN]; // 用于存储IPv4地址
    int family;
    int port = 5005; // 初始化端口号

    setlocale(LC_ALL, "");
    initscr();            // 启动 ncurses 模式
    cbreak();             // 取消行缓冲
    noecho();             // 禁止回显输入的字符
    keypad(stdscr, TRUE); // 启用功能键
    curs_set(0);          // 隐藏光标
    // mousemask(ALL_MOUSE_EVENTS, NULL); // 启用鼠标事件
    MEVENT event;

    // mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION, NULL);

    height = 24; // 窗口高度
    width = 80;
    start_y = (LINES - height) / 2; // 使窗口在屏幕中间垂直居中
    start_x = (COLS - width) / 2;   // 使窗口在屏幕中间水平居中

    win = newwin(height, width, start_y, start_x);
    box(win, 0, 0);

    mvwprintw(win, 2, 5, "Broadcast addr:   192.168.1.255                   port:    9999");
    mvwprintw(win, 3, 5, "size:             1470 bit", client_count);
    // mvwprintw(win, 4, 1, "Mode:             double                                     Limit:       no limit");
    mvwprintw(win, 4, 5, "1)UP    2)DOWN    3)double     4)limit    5)cancel      q)exit");
    // mvwprintw(win, 6, 1, "pre               next                                       set          limit    exit");
    mvwprintw(win, 5, 5, "Connected device: %d                                 Run time: %d     ", 0, 0);
    mvwprintw(win, 7, 5, "| RANK | IP             |  PORT  |  UP           |  DOWN         |");
    mvwprintw(win, 8, 6, "----------------------------------------------------------------");
    // mvwprintw(win, 15, 1, "Average bw:      0.00      |       Average bw :       0.00");
    wrefresh(win);

    if (getifaddrs(&ifaddr) == -1)
    {
        perror("getifaddrs");
        exit(EXIT_FAILURE);
    }

    // 遍历所有网络接口
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next)
    {
        if (ifa->ifa_addr == NULL)
            continue;

        family = ifa->ifa_addr->sa_family;

        // 仅处理AF_INET（IPv4）的地址
        if (family == AF_INET)
        {
            // 只处理特定的接口（如enp2s0或wlp0s20f3）
            // if (strcmp(ifa->ifa_name, "enp2s0") == 0 || strcmp(ifa->ifa_name, "wlp0s20f3") == 0)
            if (strcmp(ifa->ifa_name, "enp2s0") == 0)
            {
                // 忽略环回接口（如 lo）和未激活的接口
                if (!(ifa->ifa_flags & IFF_LOOPBACK) && (ifa->ifa_flags & IFF_UP))
                {
                    // 获取并打印IP地址
                    if (inet_ntop(family, &((struct sockaddr_in *)ifa->ifa_addr)->sin_addr, ip, sizeof(ip)) != NULL)
                    {
                        // printf("Interface: %s\tIP Address: %s\n", ifa->ifa_name, ip);
                        mvwprintw(win, 1, 5, "Server    addr:   %s                   port:    %d", ip, PORT);
                        wrefresh(win);
                    }
                }
            }
        }
    }

    freeifaddrs(ifaddr);

    nodelay(win, TRUE); // 设置窗口为非阻塞模式
    while (1)
    {
        int ch = wgetch(win); // 等待用户输入
        if (ch == 'q')
        {
            break; // 退出程序
        }
        else if (ch == '1')
        {
            global_var = 1; // 上传任务
            // mvwprintw(win, 23, 1, "切换到上传任务...");
            create_new_task(1); // 切换到上传任务
        }
        else if (ch == '2')
        {
            global_var = 2; // 上传任务
            // mvwprintw(win, 23, 1, "切换到下载任务...");
            create_new_task(2); // 切换到下载任务
        }
        else if (ch == '3')
        {
            global_var = 3; // 上传任务
            // mvwprintw(win, 23, 1, "切换到上传+下载任务...");
            create_new_task(3); // 切换到上传+下载任务
        }
        else if (ch == '4')
        {
            global_var_1 = 4; // 限速任务

            wmove(win, 6, 7); // 移动光标到窗口中的正确位置
            wrefresh(win);
            int ch1 = 0, i = 0;
            // 循环获取用户输入，直到按下 Enter 键
            while ((ch1 = wgetch(win)) != '\n')
            { // 使用 `wgetch(win)`
                if (ch1 == KEY_BACKSPACE || ch1 == 127)
                {
                    // 处理退格键
                    if (i > 0)
                    {
                        i--;
                        mvwprintw(win, 6, 7 + i, " "); // 删除字符
                        wmove(win, 6, 7 + i);          // 移动光标
                    }
                }
                else
                {
                    // 存储输入字符并显示
                    if (ch1 >= '0' && ch1 <= '9')
                    { // 确保是可打印字符
                        if (i < sizeof(input) - 1)
                        {
                            input[i++] = ch1;
                            mvwprintw(win, 6, 7 + i - 1, "%c", ch1);
                        }
                    }
                }
                wrefresh(win); // 刷新窗口
            }
            input[i] = '\0'; // 添加字符串结束符

            // 显示输入结果
            mvwprintw(win, 6, 5, "Limited: %s", input);
            input_1 = atoi(input);
            wrefresh(win);
        }

        else if (ch == '5')
        {
            global_var_1 = 5; // 无限速任务
            // mvwprintw(win, 23, 1, "切换到无限速任务...");
        }

        wrefresh(win); // 刷新窗口显示
    }

    endwin(); // 结束 ncurses 模式
    close(listen_fd);
    return;
}

int create_tcp_connection()
{
    int sockfd;
    struct sockaddr_in server_addr;

    // 创建套接字
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("create socket failed");
        exit(EXIT_FAILURE);
    }

    // 配置服务器地址
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY); // 监听所有可用的网络接口

    // 绑定服务器地址
    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("bind socket failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // 开始监听
    if (listen(sockfd, 5) < 0)
    {
        perror("listen socket failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // printf("正在监听端口 %d...\n", SERVER_PORT);
    return sockfd;
}

// 线程函数：监听TCP连接，并在全局变量发生变化时通过TCP发送数据
void *tcp_thread_func(void *arg)
{
    int server_fd = *(int *)arg;
    int client_fd;
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    int epollfd = epoll_create1(0);
    if (epollfd == -1)
    {
        perror("create epoll failed");
        exit(EXIT_FAILURE);
    }

    struct epoll_event ev, events[MAX_EVENTS];
    ev.events = EPOLLIN;
    ev.data.fd = server_fd;

    // 将监听套接字加入epoll实例
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, server_fd, &ev) == -1)
    {
        perror("epoll_ctl");
        exit(EXIT_FAILURE);
    }

    // 局部变量，存储上一次的全局变量值
    int local_var = 0;
    int local_var_1 = 0;
    int local_var_input = 0;

    while (1)
    {
        int nfds = epoll_wait(epollfd, events, MAX_EVENTS, 1000); // 等待epoll事件
        if (nfds == -1)
        {
            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < nfds; i++)
        {
            if (events[i].data.fd == server_fd)
            {
                // 接收新的连接
                client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
                if (client_fd < 0)
                {
                    perror("accept client connect failed");
                    continue;
                }
                int flag = 1;
                setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

                // 将客户端套接字加入epoll监控
                ev.events = EPOLLIN | EPOLLOUT; // 监听可读和可写事件
                ev.data.fd = client_fd;
                if (epoll_ctl(epollfd, EPOLL_CTL_ADD, client_fd, &ev) == -1)
                {
                    perror("client socket add to epoll failed");
                    close(client_fd);
                    continue;
                }
                // 将新连接的客户端套接字加入客户端列表
                if (client_count < FD_SETSIZE)
                {
                    client_fds[client_count++] = client_fd;
                }
                else
                {
                    perror("client max");
                    close(client_fd);
                }
            }
        }

        // 检查全局变量是否发生变化
        if (local_var_1 != global_var_1 || local_var != global_var || local_var_input != input_1)
        {
            int buffer[BUFFER_SIZE];
            buffer[0] = htonl(global_var_1);

            if (global_var_1 == 4)
            {
                buffer[1] = htonl(input_1);
            }
            buffer[2] = htonl(global_var);

            // 遍历所有客户端套接字
            for (int i = 0; i < nfds; i++)
            {

                if (events[i].events & EPOLLOUT) // 仅对可写的客户端发送数据
                {
                    int client_fd = events[i].data.fd;
                    if (client_fd != server_fd) // 跳过服务器套接字
                    {
                        if (send(client_fd, buffer, 12, 0) == -1)
                        {
                            perror("send data failed");
                            close(client_fd);

                            // 从epoll和客户端列表中移除该客户端
                            epoll_ctl(epollfd, EPOLL_CTL_DEL, client_fd, NULL);
                            for (int j = 0; j < client_count; j++)
                            {
                                if (client_fds[j] == client_fd)
                                {
                                    client_fds[j] = client_fds[--client_count]; // 用最后一个覆盖当前位置
                                    break;
                                }
                            }
                        }
                        else
                        {
                            // ev.events = EPOLLIN;
                            // epoll_ctl(epollfd, EPOLL_CTL_MOD, client_fd, &ev);
                        }
                    }
                }
            }

            local_var_1 = global_var_1;
            local_var = global_var;
            local_var_input = input_1;
        }

        usleep(1000); // 控制循环的频率
    }

    close(epollfd);
    return NULL;
}

int main()
{
    pthread_t thread;
    int server_fd;
    pthread_t tcp_thread;

    // 创建广播线程
    if (pthread_create(&thread, NULL, broadcast_ip, NULL) != 0)
    {
        perror("create thread failed");
        return 1;
    }

    // 等待线程完成
    // pthread_join(thread, NULL);

    // 创建tcp连接
    server_fd = create_tcp_connection();

    // 创建线程监听TCP连接
    if (pthread_create(&tcp_thread, NULL, tcp_thread_func, &server_fd) != 0)
    {
        perror("create thread failed");
        close(server_fd);
        return 1;
    }

    listen_fd = setup_epoll_server(PORT); // 初始化 epoll 和监听套接字

    ncurses_main(); // 启动 ncurses 界面，等待用户选择任务

    // 等待所有线程结束
    if (current_task == 1)
        pthread_join(upload_thread, NULL);
    if (current_task == 2)
        pthread_join(download_thread, NULL);
    if (current_task == 3)
        pthread_join(double_thread, NULL);

    pthread_join(tcp_thread, NULL);
    close(server_fd);

    return 0;
}
