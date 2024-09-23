#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <errno.h>
#include <sys/socket.h>
#include <signal.h>
#include <time.h>
#include <sys/time.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>

#define CONTROL_PORT 9999   // 主线程连接的指令端口
#define TASK_PORT 8888      // 任务线程连接的端口
#define BROADCAST_PORT 8888 // 广播接收端口
#define BUFFER_SIZE 1470
#define MAX_EVENTS 3

// 全局变量
int control_sockfd, task_sockfd, epollfd, udp_fd;
pthread_t send_thread, recv_thread, double_thread;
char server_ip[INET_ADDRSTRLEN]; // 存储服务器的 IP 地址
char current_task = 0;           // 当前任务状态：0-无任务，1-上传，2-下载，3-双向
int epollfd_local;
int task_lime;  // 限速标志
int buffer_sum; // 限速时的令牌桶容量
int tcp_udp;
// struct itimerval timer; // 定时器
struct sockaddr_in server_addr;
socklen_t addr_len = sizeof(server_addr);
struct sockaddr_in client_addr; // 用于存储客户端地址
// 声明 timer 变量
pthread_mutex_t lock;

// Leaky Bucket参数
unsigned long bucket_size;
unsigned long tokens = 0;
unsigned long tokens_double = 0;
unsigned long total_bytes = 0;
unsigned long total_bytes_double = 0;
long int bytes_read;
long int bytes_read_double;

// 定时器信号处理函数
void handle_alarm(int sig)
{
    bucket_size = buffer_sum * 1000 * 1000 / 8; // 计算桶的容量
    tokens = bucket_size;
    tokens_double = bucket_size;
    total_bytes = 0;
    total_bytes_double = 0;
}

// 发送线程函数（限速和无限速处理）
void *send_thread_func_udp(void *arg)
{
    char send_buffer[BUFFER_SIZE];
    memset(send_buffer, 'A', sizeof(send_buffer)); // 填充发送缓冲区

    // 将 udp_fd 设置为非阻塞模式
    // int flags = fcntl(udp_fd, F_GETFL, 0);
    // fcntl(udp_fd, F_SETFL, flags | O_NONBLOCK);

    signal(SIGALRM, handle_alarm);
    struct itimerval timer; // 定时器

    // 配置定时器
    timer.it_value.tv_sec = 1;
    timer.it_value.tv_usec = 0;
    timer.it_interval.tv_sec = 1;
    timer.it_interval.tv_usec = 0;

    // 启动定时器
    setitimer(ITIMER_REAL, &timer, NULL);

    while (1)
    {
        // 等待事件

        if (task_lime == 4) // 限速模式
        {

            bytes_read = sendto(udp_fd, send_buffer, sizeof(send_buffer), 0,
                                (struct sockaddr *)&server_addr, addr_len);

            while (tokens < bytes_read)
            {
                usleep(1000); // 等待 1 毫秒后重新检查
            }

            // 消耗令牌
            pthread_mutex_lock(&lock);

            tokens -= bytes_read;
            pthread_mutex_unlock(&lock);
        }
        else if (task_lime != 4)
        {
            bytes_read = sendto(udp_fd, send_buffer, sizeof(send_buffer), 0,
                                (struct sockaddr *)&server_addr, addr_len);
        }
        if (current_task != 1)
        {
            /* code */
            break;
        }
    }

    return NULL;
}

// 接收线程函数（限速和无限速处理）
void *recv_thread_func_udp(void *arg)
{
    char recv_buffer[BUFFER_SIZE];

    // // 将 udp_fd 设置为非阻塞模式
    // int flags = fcntl(udp_fd, F_GETFL, 0);
    // fcntl(udp_fd, F_SETFL, flags | O_NONBLOCK);

    while (1)
    {

        bytes_read = recvfrom(udp_fd, recv_buffer, sizeof(recv_buffer), 0,
                              (struct sockaddr *)&client_addr, &addr_len);
        if (current_task != 2)
        {
            /* code */
            break;
        }
    }

    return NULL;
}

void *double_data_to_client_recv()
{
    char recv_buffer[BUFFER_SIZE];
    while (1)
    {
        bytes_read_double = recvfrom(udp_fd, recv_buffer, sizeof(recv_buffer), 0,
                                     (struct sockaddr *)&server_addr, &addr_len);
        if (current_task != 3)
        {
            /* code */
            break;
        }
    }
}

void *double_data_to_client()
{
    // // 停止定时器
    // struct itimerval stop_timer;
    // stop_timer.it_value.tv_sec = 0;
    // stop_timer.it_value.tv_usec = 0;
    // stop_timer.it_interval.tv_sec = 0;
    // stop_timer.it_interval.tv_usec = 0;
    // setitimer(ITIMER_REAL, &stop_timer, NULL);

    // // 设置信号处理函数
    // signal(SIGALRM, handle_alarm);
    // struct itimerval timer;
    // timer.it_value.tv_sec = 1; // 1秒后触发第一次
    // timer.it_value.tv_usec = 0;
    // timer.it_interval.tv_sec = 1; // 每隔1秒触发一次
    // timer.it_interval.tv_usec = 0;
    // setitimer(ITIMER_REAL, &timer, NULL);

    char message[BUFFER_SIZE];
    memset(message, 'A', BUFFER_SIZE); // 填充消息内容

    // fcntl(clients[client_index].client_fd, F_SETFL, O_NONBLOCK);

    while (1)
    {
        if (task_lime == 4)
        {

            // 发送数据给客户端
            ssize_t sent_len = sendto(udp_fd, message, BUFFER_SIZE, 0,
                                      (struct sockaddr *)&server_addr, addr_len);

            while (tokens_double < sent_len)
            {
                usleep(1000); // 等待 1 毫秒后重新检查
            }

            // 消耗令牌
            pthread_mutex_lock(&lock);

            tokens_double -= sent_len;
            pthread_mutex_unlock(&lock);
        }

        else
        {

            // 发送数据给客户端
            ssize_t sent_len = sendto(udp_fd, message, BUFFER_SIZE, 0,
                                      (struct sockaddr *)&server_addr, addr_len);
        }
        if (current_task != 3)
        {
            /* code */
            break;
        }
    }
    // 主线程处理信号的代码
    // while (1)
    // {
    //     pause(); // 等待信号
    //     if (current_task != 3)
    //     {
    //         /* code */
    //         break;
    //     }
    // }

    // close(clients[client_index].client_fd); // 关闭连接
    return NULL;
}

// 双向线程函数（限速和无限速处理）
void *double_thread_func_udp(void *arg)
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

    pthread_t tid1;
    pthread_create(&tid1, NULL, double_data_to_client_recv, NULL);
    pthread_detach(tid1); // 分离线程，自动回收资源

    pthread_t tid;

    pthread_create(&tid, NULL, double_data_to_client, arg);
    pthread_detach(tid); // 分离线程，自动回收资源

    while (1)
    {
        pause(); // 等待信号
        if (current_task != 3)
        {
            /* code */
            break;
        }
    }

    return NULL;
}

// 根据接收指令执行对应的任务
void handle_task_udp(int task)
{
    if (current_task == 1 && pthread_cancel(send_thread) == 0)
    {
        printf("cancel send thread...\n");
    }
    else if (current_task == 2 && pthread_cancel(recv_thread) == 0)
    {
        printf("cancel recv thread...\n");
    }
    else if (current_task == 3 && pthread_cancel(double_thread) == 0)
    {
        printf("cancel double thread...\n");
    }

    current_task = task;

    if (task == 1)
    {
        pthread_create(&send_thread, NULL, send_thread_func_udp, NULL);
    }
    else if (task == 2)
    {
        pthread_create(&recv_thread, NULL, recv_thread_func_udp, NULL);
    }
    else if (task == 3)
    {
        pthread_create(&double_thread, NULL, double_thread_func_udp, NULL);
    }
}

// 创建UDP套接字并连接到服务器
int create_udp_socket_and_connect_udp(int port, const char *ip_address)
{
    int sockfd_local;

    // 创建UDP套接字
    if ((sockfd_local = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        perror("UDP socket create failed");
        exit(EXIT_FAILURE);
    }

    // 配置服务器地址
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, ip_address, &server_addr.sin_addr);

    return sockfd_local;
}

// 接收广播，获取服务器IP地址
void receive_broadcast()
{
    int broadcast_sockfd;
    struct sockaddr_in broadcast_addr;
    socklen_t addr_len = sizeof(broadcast_addr);
    char buffer[BUFFER_SIZE];

    if ((broadcast_sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        perror("create UDP socket failed");
        exit(EXIT_FAILURE);
    }

    int reuse = 1;
    if (setsockopt(broadcast_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
    {
        perror("set SO_REUSEADDR failed");
        close(broadcast_sockfd);
        exit(EXIT_FAILURE);
    }

    // 绑定到广播端口
    memset(&broadcast_addr, 0, sizeof(broadcast_addr));
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    broadcast_addr.sin_port = htons(BROADCAST_PORT);

    if (bind(broadcast_sockfd, (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr)) < 0)
    {
        perror("bind broadcast failed");
        close(broadcast_sockfd);
        exit(EXIT_FAILURE);
    }

    while (1)
    {
        int len = recvfrom(broadcast_sockfd, buffer, sizeof(buffer), 0,
                           (struct sockaddr *)&broadcast_addr, &addr_len);
        if (len > 0)
        {
            inet_ntop(AF_INET, &broadcast_addr.sin_addr, server_ip, sizeof(server_ip));
            printf("recv the server broadcast, IP: %s\n", server_ip);
            break;
        }
    }

    close(broadcast_sockfd);
}

// 创建并连接TCP套接字
int create_socket_and_connect(int port, const char *ip_address)
{
    int sockfd_local;
    struct sockaddr_in server_addr;

    if ((sockfd_local = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("socket create failed");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, ip_address, &server_addr.sin_addr);

    if (connect(sockfd_local, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("connect srever failed");
        close(sockfd_local);
        exit(EXIT_FAILURE);
    }
    printf("connected %s port %d \n", ip_address, port);

    return sockfd_local;
}

int main()
{
    // 接收广播消息获取服务器IP地址
    receive_broadcast();

    // sleep(1); // 模拟延时，等待广播消息接收完毕

    // 主线程连接指令端口
    control_sockfd = create_socket_and_connect(CONTROL_PORT, server_ip);

    // 任务线程连接任务端口
    udp_fd = create_udp_socket_and_connect_udp(TASK_PORT, server_ip);

    // 主循环，监听服务器的指令并启动相应的线程
    int command_buffer[BUFFER_SIZE];
    while (1)
    {
        memset(command_buffer, 0, sizeof(command_buffer));     // 清空缓冲区
        int len = recv(control_sockfd, command_buffer, 16, 0); // 接收指令
        if (len <= 0)
        {
            perror("recv task failed");
            close(control_sockfd);
            close(task_sockfd);
            exit(EXIT_FAILURE);
        }

        int task = command_buffer[2];
        task_lime = command_buffer[0];
        buffer_sum = command_buffer[1];
        tcp_udp = command_buffer[3];
        printf("task %d set_limit %d limit_value %d\n", task, task_lime, buffer_sum);

        handle_task_udp(task); // 根据指令执行相应任务
    }

    close(control_sockfd);
    close(task_sockfd);
    return 0;
}
