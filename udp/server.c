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
#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <signal.h>
#include <pthread.h>
#include <sys/time.h>
#include <unistd.h>

#define MAX_EVENTS 12
#define NUM_THREADS 4
#define BUFFER_SIZE 1470
#define MAX_CLIENTS 3
#define PORT 8888
#define BROADCAST_IP "192.168.1.255" // 目标广播地址
#define BROADCAST_PORT 8888          // 目标端口
#define BROADCAST_INTERVAL 1         // 每秒发送一次
#define BROADCAST_DURATION 10        // 发送持续10秒

#define SERVER_PORT 9999 // 监听的端口
// #define MAX_EVENTS 10    // 最大事件数
// #define BUFFER_SIZE 1024 // 缓冲区大小

// 函数声明
int setup_epoll_server(int port);
void handle_client_connections_up(int listen_fd);
void handle_client_connections_down(int listen_fd);
void handle_client_connections_double(int listen_fd);

int setup_epoll_server_udp(int port);

void handle_client_connections_up_udp(int udp_fd);
void handle_client_connections_down_udp(int udp_fd);
void handle_client_connections_double_udp(int udp_fd);

// 全局变量
volatile int global_var = 0;         // 全局变量，用于被监视的值
volatile int global_var_1 = 0;       // 全局变量，用于被监视的值
volatile int global_var_udp_tcp = 0; // 全局变量，用于被监视的值
char input[100] = " ";               // 输入缓冲区' ';
int input_1 = 0;                     // 限速大小

int server_fd1;
int exit_1 = 0;

struct sockaddr_in client_addr;
socklen_t client_len = sizeof(client_addr);
struct sockaddr_in server_addr;

int listen_fd, epollfd, udp_fd;
struct sockaddr_in server_addr;
struct epoll_event ev, events[MAX_EVENTS];
char client_ip[INET_ADDRSTRLEN];
int ret;
socklen_t addr_len = sizeof(server_addr);
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond_var = PTHREAD_COND_INITIALIZER;
int current_task = 0; // 0: no task, 1: upload, 2: download, 3: double
pthread_t upload_thread, download_thread, double_thread;

pthread_mutex_t lock;

typedef struct
{
    int client_fd;                   // 客户端文件描述符
    char client_ip[INET_ADDRSTRLEN]; // 客户端IP地址
    struct sockaddr_in client_addr;
    uint16_t client_port;
    int active; // 标志位，表示客户端是否活跃

    long int sum;
    long int sum1; // 数据累计值
} client_info_t;

WINDOW *win;
client_info_t clients[MAX_CLIENTS];
client_info_t send_to[MAX_CLIENTS];
int height, width, start_y, start_x;
int client_fds[FD_SETSIZE];
int client_count = 0;

unsigned long bucket_size;
unsigned long tokens[MAX_CLIENTS] = {0};
unsigned long tokens_double = 0;
unsigned long total_bytes = 0;
unsigned long total_bytes_double = 0;
long int bytes_read;
long int bytes_read_double;

// 用于多线程同步
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

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
        if (clients[i].client_port == 0)
        {
            continue;
        }

        if (clients[i].client_fd == udp_fd)
        {

            bandwidth[i] = ((double)clients[i].sum * 8.0) / 1e6;                                                                                                         // 转换为 Mbps
            // mvwprintw(win, i + 15, 1, "|IP地址:| %-16s | 上传速度为: | %-6.2lf M/s | 带宽为: | %-6.2lf Mbps", clients[i].client_ip, clients[i].sum / 1e6, bandwidth[i]); // 在窗口中显示文本
            mvwprintw(win, i + 9, 5, "| [%2d] | %-15s|  %-5d | %8.2f Mbps |      N/A      |", i, clients[i].client_ip, clients[i].client_port, bandwidth[i]);
            wrefresh(win);
            clients[i].sum = 0; // 清零累计值，准备下一次统计
        }
    }
}

void handle_alarm1(int signum)
{
    double bandwidth[MAX_CLIENTS] = {0};
    for (size_t i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i].client_port == 0)
        {
            continue;
        }
        if (clients[i].client_fd == udp_fd)
        {
            bandwidth[i] = ((double)clients[i].sum1 * 8.0) / 1e6;                                                                                                         // 转换为 Mbps
            // mvwprintw(win, i + 15, 1, "|IP地址:| %-16s | 下載速度为: | %-6.2lf M/s | 带宽为: | %-6.2lf Mbps", clients[i].client_ip, clients[i].sum1 / 1e6, bandwidth[i]); // 在窗口中显示文本
            mvwprintw(win, i + 9, 5, "| [%2d] | %-15s|  %-5d |      N/A      | %8.2f Mbps |", i, clients[i].client_ip, clients[i].client_port, bandwidth[i]);
            wrefresh(win);
            clients[i].sum1 = 0; // 清零累计值，准备下一次统计
        }
    }
}

void handle_alarm2(int signum)
{
    bucket_size = input_1 * 1000 * 1000 / 8; // 计算桶的容量
    for (size_t i = 0; i < MAX_CLIENTS; i++)
    {
        tokens[i] = bucket_size;
    }

    tokens_double = bucket_size;
    total_bytes = 0;
    total_bytes_double = 0;

    double bandwidth[MAX_CLIENTS] = {0};
    double bandwidth1[MAX_CLIENTS] = {0};
    for (size_t i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i].client_port == 0)
        {
            continue;
        }
        if (clients[i].client_fd == udp_fd)
        {
            bandwidth[i] = ((double)clients[i].sum * 8.0) / 1e6;
            bandwidth1[i] = ((double)clients[i].sum1 * 8.0) / 1e6;                                                                                                       // 转换为 Mbps
            // mvwprintw(win, i + 15, 1, "|IP地址:| %-16s | 上传带宽为: | %-6.2lf Mbps |   下载带宽为: | %-6.2lf Mbps", clients[i].client_ip, bandwidth[i], bandwidth1[i]); // 在窗口中显示文本
            mvwprintw(win, i + 9, 5, "| [%2d] | %-15s|  %-5d | %8.2f Mbps | %8.2f Mbps |", i, clients[i].client_ip, clients[i].client_port, bandwidth[i], bandwidth1[i]);
            wrefresh(win);
            clients[i].sum = 0;
            clients[i].sum1 = 0; // 清零累计值，准备下一次统计
        }
    }
}

void handle_alarm4(int sig)
{
    bucket_size = input_1 * 1000 * 1000 / 8; // 计算桶的容量
    for (size_t i = 0; i < MAX_CLIENTS; i++)
    {
        tokens[i] = bucket_size;
    }
    tokens_double = bucket_size;
    total_bytes = 0;
    total_bytes_double = 0;
    double bandwidth[MAX_CLIENTS] = {0};
    for (size_t i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i].client_port == 0)
        {
            continue;
        }
        if (clients[i].client_fd == udp_fd)
        {

            bandwidth[i] = ((double)clients[i].sum1 * 8.0) / 1e6;                                                                                                         // 转换为 Mbps
            // mvwprintw(win, i + 15, 1, "|IP地址:| %-16s | 下載速度为: | %-6.2lf M/s | 带宽为: | %-6.2lf Mbps", clients[i].client_ip, clients[i].sum1 / 1e6, bandwidth[i]); // 在窗口中显示文本
            mvwprintw(win, i + 9, 5, "| [%2d] | %-15s|  %-5d | %8.2f Mbps |      N/A      |", i, clients[i].client_ip, clients[i].client_port, bandwidth[i]);
            wrefresh(win);
            clients[i].sum1 = 0; // 清零累计值，准备下一次统计
        }
    }
}

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
        perror("socket create failed");
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
            perror("send broadcast failed");
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
void handle_client_connections_up_udp(int udp_fd)
{
    // 停止定时器
    struct itimerval stop_timer;
    stop_timer.it_value.tv_sec = 0;
    stop_timer.it_value.tv_usec = 0;
    stop_timer.it_interval.tv_sec = 0;
    stop_timer.it_interval.tv_usec = 0;
    setitimer(ITIMER_REAL, &stop_timer, NULL);

    // 设置信号处理函数
    signal(SIGALRM, handle_alarm);
    struct itimerval timer;
    timer.it_value.tv_sec = 1; // 1秒后触发第一次
    timer.it_value.tv_usec = 0;
    timer.it_interval.tv_sec = 1; // 每隔1秒触发一次
    timer.it_interval.tv_usec = 0;
    setitimer(ITIMER_REAL, &timer, NULL);
    // 设置 udp_fd 为非阻塞模式
    // int flags = fcntl(udp_fd, F_GETFL, 0);
    // fcntl(udp_fd, F_SETFL, flags | O_NONBLOCK);

    // 将 udp_fd 添加到 epoll 监控列表中，监听可读事件

    char client_ip[INET_ADDRSTRLEN];
    char buffer[BUFFER_SIZE];

    while (1)
    {

        // 如果事件是可读事件
        memset(&client_addr, 0, sizeof(client_addr));
        client_len = sizeof(client_addr);

        // 使用 recvfrom 接收 UDP 数据包
        ssize_t len = recvfrom(udp_fd, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&client_addr, &client_len);
        if (len < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // 当前没有可用数据
                continue;
            }
            else
            {
                perror("recvfrom failed");
                continue;
            }
        }

        // 转换客户端 IP 地址为字符串
        inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);

        // 检查是否已有客户端的IP在clients列表中，或找到空的槽位
        int client_index = -1;
        for (int j = 0; j < MAX_CLIENTS; j++)
        {
            if (clients[j].client_fd == 0)
            {
                // 找到空槽位，存储客户端信息
                client_index = j;
                clients[j].client_fd = udp_fd;
                strncpy(clients[j].client_ip, client_ip, INET_ADDRSTRLEN);
                clients[j].client_addr = *(struct sockaddr_in *)&client_addr;
                clients[j].client_port = ntohs(client_addr.sin_port); // 保存客户端端口
                clients[j].sum = 0;
                clients[j].sum1 = 0;
                break;
            }
            else if (strncmp(clients[j].client_ip, client_ip, INET_ADDRSTRLEN) == 0 && clients[j].client_port == ntohs(client_addr.sin_port))
            {
                // 已经存在的客户端，更新其索引
                client_index = j;
                break;
            }
        }

        if (client_index != -1)
        {
            // 更新客户端的总接收字节数
            pthread_mutex_lock(&lock);

            clients[client_index].sum += len; // 更新已发送字节数

            pthread_mutex_unlock(&lock);
        }
        else
        {
            printf("client max\n");
        }
        if (global_var != 1)
        {
            /* code */
            break;
        }
    }

    // 清理 epoll 文件描述符
    return;
}

void *send_data_to_client(void *arg)
{

    int client_index = *(int *)arg;
    free(arg); // 释放分配的内存

    char message[BUFFER_SIZE];
    memset(message, 'A', BUFFER_SIZE); // 填充消息内容

    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    client_addr = clients[client_index].client_addr;
    // fcntl(clients[client_index].client_fd, F_SETFL, O_NONBLOCK);

    // 停止定时器
    struct itimerval stop_timer;
    stop_timer.it_value.tv_sec = 0;
    stop_timer.it_value.tv_usec = 0;
    stop_timer.it_interval.tv_sec = 0;
    stop_timer.it_interval.tv_usec = 0;
    setitimer(ITIMER_REAL, &stop_timer, NULL);

    signal(SIGALRM, handle_alarm4);
    struct itimerval timer;
    timer.it_value.tv_sec = 1; // 1秒后触发第一次
    timer.it_value.tv_usec = 0;
    timer.it_interval.tv_sec = 1; // 每隔1秒触发一次
    timer.it_interval.tv_usec = 0;
    setitimer(ITIMER_REAL, &timer, NULL);

    while (1)
    {
        if (global_var_1 == 4)
        {

            // 发送数据给客户端
            ssize_t sent_len = sendto(clients[client_index].client_fd, message, BUFFER_SIZE, 0,
                                      (struct sockaddr *)&client_addr, client_len);

            pthread_mutex_lock(&lock);

            clients[client_index].sum1 += sent_len; // 更新已发送字节数

            pthread_mutex_unlock(&lock);

            while (tokens[client_index] < sent_len)
            {
                usleep(1000); // 等待 1 毫秒后重新检查
            }

            // 消耗令牌
            tokens[client_index] -= sent_len;
        }
        else
        {

            // 发送数据给客户端
            ssize_t sent_len = sendto(clients[client_index].client_fd, message, BUFFER_SIZE, 0,
                                      (struct sockaddr *)&client_addr, client_len);

            pthread_mutex_lock(&lock);

            clients[client_index].sum1 += sent_len; // 更新已发送字节数

            pthread_mutex_unlock(&lock);
        }
        if (global_var != 2)
        {
            /* code */
            break;
        }

        // usleep(1000); // 可调整发送频率，防止过快占用带宽
    }
    // 主线程处理信号的代码
    while (1)
    {
        pause(); // 等待信号
        if (global_var != 2)
        {
            /* code */
            break;
        }
    }

    // close(clients[client_index].client_fd); // 关闭连接
    return NULL;
}

void handle_client_connections_down_udp(int udp_fd)
{

    pthread_t tid[MAX_CLIENTS];
    // // 设置 udp_fd 为非阻塞模式
    // int flags = fcntl(udp_fd, F_GETFL, 0);
    // fcntl(udp_fd, F_SETFL, flags | O_NONBLOCK);

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i].client_fd == udp_fd)
        {

            int *arg = malloc(sizeof(int));
            *arg = i;
            pthread_create(&tid[i], NULL, send_data_to_client, arg);
            pthread_detach(tid[i]); // 分离线程，自动回收资源
        }
    }
}

void *double_data_to_client(void *arg)
{
    // // 停止定时器
    // struct itimerval stop_timer;
    // stop_timer.it_value.tv_sec = 0;
    // stop_timer.it_value.tv_usec = 0;
    // stop_timer.it_interval.tv_sec = 0;
    // stop_timer.it_interval.tv_usec = 0;
    // setitimer(ITIMER_REAL, &stop_timer, NULL);

    // // 设置信号处理函数
    // signal(SIGALRM, handle_alarm2);
    // struct itimerval timer;
    // timer.it_value.tv_sec = 1; // 1秒后触发第一次
    // timer.it_value.tv_usec = 0;
    // timer.it_interval.tv_sec = 1; // 每隔1秒触发一次
    // timer.it_interval.tv_usec = 0;
    // setitimer(ITIMER_REAL, &timer, NULL);

    int client_index = *(int *)arg;
    free(arg); // 释放分配的内存

    char message[BUFFER_SIZE];
    memset(message, 'A', BUFFER_SIZE); // 填充消息内容

    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    client_addr = clients[client_index].client_addr;
    // fcntl(clients[client_index].client_fd, F_SETFL, O_NONBLOCK);

    while (1)
    {
        if (global_var_1 == 4)
        {

            // 发送数据给客户端
            ssize_t sent_len = sendto(clients[client_index].client_fd, message, BUFFER_SIZE, 0,
                                      (struct sockaddr *)&client_addr, client_len);

            pthread_mutex_lock(&lock);

            clients[client_index].sum1 += sent_len; // 更新已发送字节数

            pthread_mutex_unlock(&lock);

            while (tokens[client_index] < sent_len)
            {
                usleep(1000); // 等待 1 毫秒后重新检查
            }

            // 消耗令牌
            tokens[client_index] -= sent_len;
        }
        else
        {

            // 发送数据给客户端
            ssize_t sent_len = sendto(clients[client_index].client_fd, message, BUFFER_SIZE, 0,
                                      (struct sockaddr *)&client_addr, client_len);

            pthread_mutex_lock(&lock);

            clients[client_index].sum1 += sent_len; // 更新已发送字节数

            pthread_mutex_unlock(&lock);
        }
        if (global_var != 3)
        {
            /* code */
            break;
        }
        // while (1)
        // {
        //     pause(); // 等待信号
        //     if (global_var != 3)
        //     {
        //         /* code */
        //         break;
        //     }
        // }

        // usleep(1); // 可调整发送频率，防止过快占用带宽
    }

    // close(clients[client_index].client_fd); // 关闭连接
    return NULL;
}

void *double_data_to_client_recv()
{

    char client_ip[INET_ADDRSTRLEN];
    char message[BUFFER_SIZE];
    memset(message, 'A', sizeof(message)); // 用字符 'A' 填充缓冲区
    size_t message_len = sizeof(message);  // 消息长度

    // 设置 udp_fd 为非阻塞模式
    // int flags = fcntl(udp_fd, F_GETFL, 0);
    // fcntl(udp_fd, F_SETFL, flags | O_NONBLOCK);

    // 事件循环
    while (1)
    {

        char recv_buffer[BUFFER_SIZE];
        memset(&client_addr, 0, sizeof(client_addr));
        client_len = sizeof(client_addr);

        // 使用 recvfrom 接收 UDP 数据
        ssize_t recv_len = recvfrom(udp_fd, recv_buffer, sizeof(recv_buffer), 0,
                                    (struct sockaddr *)&client_addr, &client_len);
        if (recv_len < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // 没有数据可读，继续处理其他事件
                continue;
            }
            perror("recvfrom failed");
            continue;
        }

        // 将客户端的 IP 地址转换为字符串
        inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);

        // 查找是否已有该客户端的 IP 地址记录
        int client_index = -1;
        for (int j = 0; j < MAX_CLIENTS; j++)
        {
            if (clients[j].client_fd != 0 &&
                strncmp(clients[j].client_ip, client_ip, INET_ADDRSTRLEN) == 0 &&
                clients[j].client_port == ntohs(client_addr.sin_port))
            {
                client_index = j;

                pthread_mutex_lock(&lock);

                clients[client_index].sum += recv_len; // 更新已发送字节数

                pthread_mutex_unlock(&lock);

                break;
            }
        }

        if (global_var != 3)
        {
            /* code */
            break;
        }
    }
    // while (1)
    // {
    //     pause(); // 等待信号
    //     if (global_var != 3)
    //     {
    //         /* code */
    //         break;
    //     }
    // }
}

void handle_client_connections_double_udp(int udp_fd)
{

    // 停止定时器
    struct itimerval stop_timer;
    stop_timer.it_value.tv_sec = 0;
    stop_timer.it_value.tv_usec = 0;
    stop_timer.it_interval.tv_sec = 0;
    stop_timer.it_interval.tv_usec = 0;
    setitimer(ITIMER_REAL, &stop_timer, NULL);

    // 设置信号处理函数
    signal(SIGALRM, handle_alarm2);
    struct itimerval timer;
    timer.it_value.tv_sec = 1; // 1秒后触发第一次
    timer.it_value.tv_usec = 0;
    timer.it_interval.tv_sec = 1; // 每隔1秒触发一次
    timer.it_interval.tv_usec = 0;
    setitimer(ITIMER_REAL, &timer, NULL);

    pthread_t tid1;
    pthread_create(&tid1, NULL, double_data_to_client_recv, NULL);
    pthread_detach(tid1); // 分离线程，自动回收资源

    pthread_t tid[MAX_CLIENTS];

    // 找到客户端并发送填充字符 'A' 的数据
    for (int j = 0; j < MAX_CLIENTS; j++)
    {
        for (int i = 0; i < MAX_CLIENTS; i++)
        {

            int *arg = malloc(sizeof(int));
            *arg = i;
            pthread_create(&tid[i], NULL, double_data_to_client, arg);
            pthread_detach(tid[i]); // 分离线程，自动回收资源
        }
    }
    while (1)
    {
        pause(); // 等待信号
        if (global_var != 3)
        {
            /* code */
            break;
        }
    }
}

// 上传线程函数
void *upload_thread_func_udp(void *arg)
{
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    // mvwprintw(win, 25, 1, "上传线程启动...\n");
    pthread_cleanup_push(cleanup, NULL);
    handle_client_connections_up_udp(udp_fd); // 执行上传任务
    pthread_cleanup_pop(1);
    // printf("上传任务完成，线程终止...\n");
    return NULL;
}

// 下载线程函数
void *download_thread_func_udp(void *arg)
{
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    // mvwprintw(win, 25, 1, "下载线程启动...\n");
    pthread_cleanup_push(cleanup, NULL);
    handle_client_connections_down_udp(udp_fd); // 执行下载任务
    pthread_cleanup_pop(1);
    // printf("下载任务完成，线程终止...\n");
    return NULL;
}

// 上传+下载线程函数
void *double_thread_func_udp(void *arg)
{
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    // mvwprintw(win, 25, 1, "上传+下载线程启动...\n");
    pthread_cleanup_push(cleanup, NULL);
    handle_client_connections_double_udp(udp_fd); // 执行上传+下载任务
    pthread_cleanup_pop(1);
    // printf("上传+下载任务完成，线程终止...\n");
    return NULL;
}

void create_new_task_udp(int task)
{
    if (current_task == 1 && pthread_cancel(upload_thread) == 0)
    {
        mvwprintw(win, 24, 1, "cancel up thread .......\n");
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
        pthread_create(&upload_thread, NULL, upload_thread_func_udp, NULL);
    }
    else if (task == 2)
    {
        pthread_create(&download_thread, NULL, download_thread_func_udp, NULL);
    }
    else if (task == 3)
    {
        pthread_create(&double_thread, NULL, double_thread_func_udp, NULL);
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
    // mvwprintw(win, 5, 5, "Connected device: %d                                 Run time: %d     ", 0, 0);
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
            // 退出程序
            exit_1 = 1; //
            delwin(win);
            endwin();
            close(server_fd1);
            close(udp_fd);
            return;
        }
        else if (ch == '1')
        {
            global_var = 1; // 上传任务

            create_new_task_udp(1); // 切换到上传任务
        }
        else if (ch == '2')
        {
            global_var = 2; // 上传任务

            create_new_task_udp(2); // 切换到上传任务
        }
        else if (ch == '3')
        {
            global_var = 3; // 上传任务

            create_new_task_udp(3); // 切换到上传任务
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
    int opt = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        perror("setsockopt failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    // 绑定服务器地址
    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("bind port failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // 开始监听
    if (listen(sockfd, 5) < 0)
    {
        perror("listen failed");
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
        perror("epoll_ctl failed");
        exit(EXIT_FAILURE);
    }

    // 局部变量，存储上一次的全局变量值
    int local_var = 0;
    int local_var_1 = 0;
    int local_var_input = 0;
    int local_var_udp_tcp = 0;

    while (1)
    {
        int nfds = epoll_wait(epollfd, events, MAX_EVENTS, 1000); // 等待epoll事件
        if (nfds == -1)
        {
            perror("epoll_wait failed");
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
                    perror("client connect failed");
                    continue;
                }
                int flag = 1;
                setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

                // 将客户端套接字加入epoll监控
                ev.events = EPOLLIN | EPOLLOUT; // 监听可读和可写事件
                ev.data.fd = client_fd;
                if (epoll_ctl(epollfd, EPOLL_CTL_ADD, client_fd, &ev) == -1)
                {
                    perror("add client socket epoll failed");
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
            buffer[0] = global_var_1;

            if (global_var_1 == 4)
            {
                buffer[1] = input_1;
            }
            buffer[2] = global_var;

            // 遍历所有客户端套接字
            for (int i = 0; i < nfds; i++)
            {

                if (events[i].events & EPOLLOUT) // 仅对可写的客户端发送数据
                {
                    int client_fd = events[i].data.fd;
                    if (client_fd != server_fd) // 跳过服务器套接字
                    {
                        if (send(client_fd, buffer, 16, 0) == -1)
                        {
                            perror("发送数据失败");
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
        }

        local_var_1 = global_var_1;
        local_var = global_var;
        local_var_input = input_1;
        local_var_udp_tcp = global_var_udp_tcp;

        usleep(1000); // 控制循环的频率
    }

    close(epollfd);
    return NULL;
}

int setup_epoll_server_udp(int port)
{

    // 创建 UDP 套接字
    udp_fd = socket(AF_INET, SOCK_DGRAM, 0); // 使用 SOCK_DGRAM 创建 UDP 套接字
    if (udp_fd == -1)
    {
        perror("socket");
        return -1;
    }

    // 设置地址重用选项（可选）
    int reuse = 1;
    if (setsockopt(udp_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
    {
        perror("setsockopt");
        close(udp_fd);
        return -1;
    }

    // 配置服务器地址
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY); // 绑定到任意可用 IP 地址

    // 绑定地址和端口
    if (bind(udp_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
    {
        perror("bind");
        close(udp_fd);
        return -1;
    }

    return udp_fd;
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
    server_fd = create_tcp_connection();
    server_fd1 = server_fd;

    udp_fd = setup_epoll_server_udp(PORT); // 初始化 epoll 和监听套接字

    // 创建线程监听TCP连接
    if (pthread_create(&tcp_thread, NULL, tcp_thread_func, &server_fd) != 0)
    {
        perror("create tcp thread failed");
        close(server_fd);
        return 1;
    }

    ncurses_main(); // 启动 ncurses 界面，等待用户选择任务
    if (exit_1 == 1)
    {
        return 0;
    }

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
