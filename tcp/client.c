#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <signal.h>
#include <asm-generic/socket.h>

#include <time.h>

#define CONTROL_PORT 9999   // 主线程连接的指令端口
#define TASK_PORT 8888      // 任务线程连接的端口
#define BROADCAST_PORT 8888 // 广播接收端口
#define BUFFER_SIZE 2048
#define MAX_EVENTS 12

// 全局变量声明
int control_sockfd, task_sockfd, epollfd;          // 套接字描述符
pthread_t send_thread, recv_thread, double_thread; // 线程
char server_ip[INET_ADDRSTRLEN];                   // 存储服务器的 IP 地址
char current_task = 0;
int epollfd_local; // 0: no task, 1: upload, 2: download, 3: double，表示当前正在执行的任务类型
int task_lime;
int buffer_sum;
struct itimerval timer; // 声明 timer 变量

// Leaky Bucket参数
unsigned long bucket_size;       // 桶的容量，最大可存放的字节数
unsigned long tokens = 0;        // 桶中的当前字节数
unsigned long tokens_double = 0; // 桶中的当前字节数
unsigned long total_bytes = 0;   // 总接收字节数
unsigned long total_bytes_double = 0;
long int bytes_read;
long int bytes_read_double;

void handle_alarm(int sig)
{
    bucket_size = buffer_sum * 1024 * 1024 / 8;
    // 每秒增加一定的令牌（即允许通过的字节数）
    tokens = bucket_size;
    tokens_double = bucket_size; // 双令牌容量

    total_bytes = 0; // 重置接收字节数
    total_bytes_double = 0;
}

// 发送线程函数
void *send_thread_func(void *arg)
{
    int epollfd_local = *(int *)arg;               // 从参数中获取 epoll 文件描述符
    char send_buffer[BUFFER_SIZE];                 // 存储发送的数据
    struct epoll_event events[MAX_EVENTS];         // 存储 epoll 事件
    memset(send_buffer, 'A', sizeof(send_buffer)); // 填充发送缓冲区

    while (1)
    {
        int nfds = epoll_wait(epollfd_local, events, MAX_EVENTS, -1); // 等待 epoll 事件触发
        if (nfds == -1)
        {
            if (errno == EINTR) // 如果被信号中断，继续等待
                continue;
            perror("epoll_wait failed");
            break;
        }

        // 遍历所有就绪的文件描述符
        for (int i = 0; i < nfds; i++)
        {
            if (events[i].events & EPOLLOUT) // 如果套接字可以写入
            {
                // 模拟发送数据
                // snprintf(send_buffer, sizeof(send_buffer), "客户端发送的数据: %ld", random());
                memset(send_buffer, 'A', sizeof(send_buffer)); // 填充发送缓冲区
                                                               // if (send(task_sockfd, send_buffer, strlen(send_buffer), 0) == -1) // 通过套接字发送数据
                                                               // {
                                                               //     perror("发送失败");
                                                               //     break;
                                                               // }
                if (task_lime == 4)
                {
                    /* code */
                    signal(SIGALRM, handle_alarm);

                    // 配置定时器
                    timer.it_value.tv_sec = 1;
                    timer.it_value.tv_usec = 0;
                    timer.it_interval.tv_sec = 1;
                    timer.it_interval.tv_usec = 0;

                    // 启动定时器
                    setitimer(ITIMER_REAL, &timer, NULL);

                    while ((bytes_read = send(task_sockfd, send_buffer, sizeof(send_buffer), 0)) > 0)
                    {
                        // 等待直到有足够的令牌
                        while (tokens < bytes_read)
                        {
                            usleep(10); // 等待 1 毫秒后重新检查
                        }

                        // 消耗令牌
                        tokens -= bytes_read;

                        // 累加接收到的数据量
                        total_bytes += bytes_read;
                    }

                    if (bytes_read < 0)
                    {
                        perror("read");
                        break;
                    }
                    // printf("接收到的数据: %s\n", recv_buffer); // 打印接收到的数据
                }
                else
                {
                    bytes_read = bytes_read = send(task_sockfd, send_buffer, sizeof(send_buffer), 0);
                    if (bytes_read < 0)
                    {
                        perror("send");
                        break;
                    }
                }
                // printf("发送数据: %s\n", send_buffer);
            }
        }
        // usleep(1); // 模拟延时
    }
    return NULL;
}

// 接收线程函数
void *recv_thread_func(void *arg)
{
    int epollfd_local = *(int *)arg;       // 从参数中获取 epoll 文件描述符
    char recv_buffer[BUFFER_SIZE];         // 存储接收到的数据
    struct epoll_event events[MAX_EVENTS]; // 存储 epoll 事件

    while (1)
    {
        int nfds = epoll_wait(epollfd_local, events, MAX_EVENTS, -1); // 等待 epoll 事件触发
        if (nfds == -1)
        {
            if (errno == EINTR) // 如果被信号中断，继续等待
                continue;
            perror("epoll_wait failed");
            break;
        }

        // 遍历所有就绪的文件描述符
        for (int i = 0; i < nfds; i++)
        {
            if (events[i].events & EPOLLIN) // 如果套接字可以读入
            {
                if (task_lime == 4)
                {
                    /* code */
                    signal(SIGALRM, handle_alarm);

                    // 配置定时器
                    timer.it_value.tv_sec = 1;
                    timer.it_value.tv_usec = 0;
                    timer.it_interval.tv_sec = 1;
                    timer.it_interval.tv_usec = 0;

                    // 启动定时器
                    setitimer(ITIMER_REAL, &timer, NULL);

                    while ((bytes_read = recv(task_sockfd, recv_buffer, sizeof(recv_buffer), 0)) > 0)
                    {
                        // 等待直到有足够的令牌
                        while (tokens < bytes_read)
                        {
                            usleep(10); // 等待 1 毫秒后重新检查
                        }

                        // 消耗令牌
                        tokens -= bytes_read;

                        // 累加接收到的数据量
                        total_bytes += bytes_read;
                    }

                    if (bytes_read < 0)
                    {
                        perror("read");
                        break;
                    }
                    // printf("接收到的数据: %s\n", recv_buffer); // 打印接收到的数据
                }
                else
                {
                    bytes_read = recv(task_sockfd, recv_buffer, sizeof(recv_buffer), 0);
                    if (bytes_read < 0)
                    {
                        perror("read");
                        break;
                    }
                }
            }
        }
    }
    return NULL;
}

// 发送+接收线程函数，用于同时处理发送和接收
void *double_thread_func(void *arg)
{
    int epollfd_local = *(int *)arg;       // 从参数中获取 epoll 文件描述符
    char send_buffer[BUFFER_SIZE];         // 发送缓冲区
    char recv_buffer[BUFFER_SIZE];         // 接收缓冲区
    struct epoll_event events[MAX_EVENTS]; // epoll 事件结构体

    while (1)
    {
        int nfds = epoll_wait(epollfd_local, events, MAX_EVENTS, -1); // 等待 epoll 事件触发
        if (nfds == -1)
        {
            if (errno == EINTR) // 如果被信号中断，继续等待
                continue;
            perror("epoll_wait failed");
            break;
        }

        // 遍历所有就绪的文件描述符
        for (int i = 0; i < nfds; i++)
        {
            if (events[i].events & EPOLLOUT) // 套接字可写
            {
                if (task_lime == 4)
                {
                    /* code */
                    signal(SIGALRM, handle_alarm);

                    // 配置定时器
                    timer.it_value.tv_sec = 1;
                    timer.it_value.tv_usec = 0;
                    timer.it_interval.tv_sec = 1;
                    timer.it_interval.tv_usec = 0;

                    // 启动定时器
                    setitimer(ITIMER_REAL, &timer, NULL);

                    while ((bytes_read = send(task_sockfd, send_buffer, sizeof(send_buffer), 0)) > 0)
                    {
                        // 等待直到有足够的令牌
                        // while (tokens < bytes_read)
                        // {
                        //     usleep(1000); // 等待 1 毫秒后重新检查

                        // }
                        if (tokens < bytes_read)
                        {
                            /* code */
                            break;
                        }

                        // 消耗令牌
                        tokens -= bytes_read;

                        // 累加接收到的数据量
                        total_bytes += bytes_read;
                    }

                    if (bytes_read < 0)
                    {
                        perror("read");
                        break;
                    }
                    // printf("接收到的数据: %s\n", recv_buffer); // 打印接收到的数据
                }
                else
                {
                    bytes_read = bytes_read = send(task_sockfd, send_buffer, sizeof(send_buffer), 0);
                    if (bytes_read < 0)
                    {
                        perror("send");
                        break;
                    }
                }
                // printf("发送数据: %s\n", send_buffer);
            }
            if (events[i].events & EPOLLIN) // 套接字可读
            {
                // memset(recv_buffer, 0, sizeof(recv_buffer)); // 清空接收缓冲区
                if (task_lime == 4)
                {
                    /* code */
                    signal(SIGALRM, handle_alarm);

                    // 配置定时器
                    timer.it_value.tv_sec = 1;
                    timer.it_value.tv_usec = 0;
                    timer.it_interval.tv_sec = 1;
                    timer.it_interval.tv_usec = 0;

                    // 启动定时器
                    setitimer(ITIMER_REAL, &timer, NULL);

                    while ((bytes_read_double = recv(task_sockfd, recv_buffer, sizeof(recv_buffer), 0)) > 0)
                    {
                        // 等待直到有足够的令牌
                        // while (tokens_double < bytes_read_double)
                        // {
                        //     usleep(1000); // 等待 1 毫秒后重新检查
                        // }
                        if (tokens_double < bytes_read_double)
                        {
                            /* code */
                            break;
                        }

                        // 消耗令牌
                        tokens_double -= bytes_read_double;

                        // 累加接收到的数据量
                        total_bytes_double += bytes_read_double;
                    }

                    if (bytes_read < 0)
                    {
                        perror("read");
                        break;
                    }
                    // printf("接收到的数据: %s\n", recv_buffer); // 打印接收到的数据
                }
                else
                {
                    bytes_read = recv(task_sockfd, recv_buffer, sizeof(recv_buffer), 0);
                    if (bytes_read < 0)
                    {
                        perror("read");
                        break;
                    }
                }
                if (task_lime == 4)
                {
                    while (tokens <= bytes_read && tokens_double <= bytes_read_double)
                    {
                        usleep(10); // 等待 1 毫秒后重新检查
                    }
                }
            }

            // printf("接收到的数据: %s\n", recv_buffer); // 打印接收到的数据
        }
        // usleep(1); // 模拟延时
    }
    return NULL;
}

// 根据收到的指令启动相应的线程
void handle_task(int task)
{
    // 如果当前有任务正在运行，取消它
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

    // 创建 epoll 实例用于任务线程
    epollfd_local = epoll_create1(0);
    if (epollfd_local == -1)
    {
        perror("epoll_create1 failed");
        exit(EXIT_FAILURE);
    }

    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLOUT; // 监听可读和可写事件
    ev.data.fd = task_sockfd;

    // 将套接字加入 epoll 监控
    if (epoll_ctl(epollfd_local, EPOLL_CTL_ADD, task_sockfd, &ev) == -1)
    {
        perror("epoll_ctl failed");
        close(task_sockfd);
        exit(EXIT_FAILURE);
    }

    current_task = task; // 更新当前任务

    // 根据新任务创建对应的线程
    if (task == 1)
    {
        pthread_create(&send_thread, NULL, send_thread_func, &epollfd_local);
    }
    else if (task == 2)
    {
        pthread_create(&recv_thread, NULL, recv_thread_func, &epollfd_local);
    }
    else if (task == 3)
    {
        pthread_create(&double_thread, NULL, double_thread_func, &epollfd_local);
    }
}

// 创建并连接到指定端口
int create_socket_and_connect(int port, const char *ip_address)
{
    int sockfd_local;
    struct sockaddr_in server_addr;

    // 创建套接字
    if ((sockfd_local = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("socket create failed");
        exit(EXIT_FAILURE);
    }

    // 配置服务器地址
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, ip_address, &server_addr.sin_addr); // 转换 IP 地址为网络字节序

    // 连接服务器
    if (connect(sockfd_local, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("connect server failed");
        close(sockfd_local);
        exit(EXIT_FAILURE);
    }
    printf("connected %s port %d \n", ip_address, port);

    return sockfd_local;
}

// 循环接收广播，获取服务器 IP 地址
void receive_broadcast()
{
    int broadcast_sockfd;
    struct sockaddr_in broadcast_addr;
    socklen_t addr_len = sizeof(broadcast_addr);
    char buffer[BUFFER_SIZE];

    // 创建 UDP 套接字
    if ((broadcast_sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        perror("create UDP socket failed");
        exit(EXIT_FAILURE);
    }

    // 启用 SO_REUSEADDR 选项
    int reuse = 1;
    if (setsockopt(broadcast_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
    {
        perror("set SO_REUSEADDR failed");
        close(broadcast_sockfd);
        exit(EXIT_FAILURE);
    }

    // 绑定到广播端口8888
    memset(&broadcast_addr, 0, sizeof(broadcast_addr));
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_addr.s_addr = htonl(INADDR_ANY); // 绑定到任何可用地址
    broadcast_addr.sin_port = htons(8888);              // 绑定到端口8888

    if (bind(broadcast_sockfd, (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr)) < 0)
    {
        perror("bind broadcast failed");
        close(broadcast_sockfd);
        exit(EXIT_FAILURE);
    }

    printf("waiting server broadcast ...\n");
    // 循环接收广播消息，直到成功接收到服务器的 IP 地址
    while (1)
    {
        int len = recvfrom(broadcast_sockfd, buffer, sizeof(buffer), 0, (struct sockaddr *)&broadcast_addr, &addr_len); // 接收广播消息
        if (len < 0)
        {
            perror("recv broadcast failed");
        }
        else
        {
            buffer[len] = '\0';                                                         // 确保接收到的消息以 '\0' 结尾
            inet_ntop(AF_INET, &broadcast_addr.sin_addr, server_ip, sizeof(server_ip)); // 从广播消息中提取 IP 地址
            printf("recv the server broadcast IP : %s\n", server_ip);
            break; // 一旦接收到 IP 地址，跳出循环
        }
    }

    close(broadcast_sockfd); // 关闭广播套接字
}

int main()
{

    // 接收广播消息并获取服务器IP地址
    receive_broadcast();

    sleep(1); // 模拟延时，等待广播消息接收完毕

    // 主线程连接指令端口 9999
    control_sockfd = create_socket_and_connect(CONTROL_PORT, server_ip);

    // 任务线程连接任务端口 8888
    task_sockfd = create_socket_and_connect(TASK_PORT, server_ip);

    // 主循环，监听服务器的指令并启动相应的线程
    int command_buffer[BUFFER_SIZE];
    while (1)
    {
        memset(command_buffer, 0, sizeof(command_buffer));     // 清空缓冲区
        int len = recv(control_sockfd, command_buffer, 12, 0); // 从指令端口接收数据
        if (len == -1)
        {
            perror("recv order failed");
            close(control_sockfd);
            close(task_sockfd);
            exit(EXIT_FAILURE);
        }
        else if (len < 0) // 服务器断开连接
        {
            printf("server disconnected\n");
            close(control_sockfd);
            close(task_sockfd);
            exit(EXIT_SUCCESS);
        }

        // char task = atoi(command_buffer); // 假设服务器发送的指令是1, 2 或 3
        int task = ntohl(command_buffer[2]);
        task_lime = ntohl(command_buffer[0]);
        buffer_sum = ntohl(command_buffer[1]);

        printf("mode: %d buffer_sum: %d limit: %d\n", task, buffer_sum, task_lime);

        // task = '1';

        handle_task(task); // 根据指令执行相应任务
    }

    close(control_sockfd);
    close(task_sockfd);
    return 0;
}
