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

WINDOW * main_win;
test_mode_t current_mode = MODE_DOUBLE;
int speed_limit = -1;

typedef struct {
    int fd;
    long total_bytes_up;
    long total_bytes_down;
    struct timeval start;
    pthread_mutex_t lock;
    char ip[INET_ADDRSTRLEN];
    int port;
    int active;
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
    int rank = 1;

    for (size_t i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active) {
            pthread_mutex_lock(&clients[i].lock);

            timersub(&now, &clients[i].start, &elapsed);
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

            mvwprintw(main_win, rank + 10, 1, "| [%2d] | %s\t| %d | %8.2f Mbps |", rank, clients[i].ip, clients[i].port, bandwidth_mbps);
            rank ++;
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
        mvwprintw(main_win, j + 10, 1, "|      | \t\t| \t\t | \t\t |");
    }
    wrefresh(main_win);
}

void* handle_client(void * arg) {
    client_info_t* client = (client_info_t*)arg;
    char buffer[BUFFER_SIZE];
    ssize_t len;
    struct timeval last_send_time;

    gettimeofday(&last_send_time, NULL);

    while ((len = recv(client->fd, buffer, BUFFER_SIZE, 0)) > 0) {
        pthread_mutex_lock(&client->lock);

        if (speed_limit > 0) {
            struct timeval now;
            gettimeofday(&now, NULL);
            long delta = (now.tv_sec - last_send_time.tv_sec) * 1000000 + (now.tv_usec - last_send_time.tv_usec);
            long limit_interval = (long)((1e6 / speed_limit) * BUFFER_SIZE / (1470 * 8));

            if (delta < limit_interval) {
                usleep(limit_interval - delta);
            }
            gettimeofday(&last_send_time, NULL);
        }

        if (current_mode == MODE_DOUBLE || current_mode == MODE_UP) {
            client->total_bytes_up += len;
        }else if (current_mode == MODE_DOWN) {
            client->total_bytes_down += len;
        }

        pthread_mutex_unlock(&client->lock);
    }

    close(client->fd);
    client->active = 0;
    pthread_exit(NULL);
}

void handle_keyboard_event(int ch) {
    static int selected_option = 0;
    char limit_str[10];

    switch (ch) {
        case KEY_LEFT:
            if (selected_option > 0) selected_option --;
            break;
        case KEY_RIGHT:
            if (selected_option < 1) selected_option ++;
            break;
        case 10:
            if (selected_option == 0) {
                switch (current_mode) {
                    case MODE_DOUBLE: current_mode = MODE_UP; break;
                    case MODE_UP: current_mode = MODE_DOWN; break;
                    case MODE_DOWN: current_mode = MODE_DOUBLE; break;
                }
                mvwprintw(main_win, 6, 1, "Mode: %s \t\t\t\t Limit: %s", get_mode_string(current_mode), speed_limit == -1 ? "nolimit":"limited");
            } else if (selected_option == 1) {
                mvwprintw(main_win, 7, 1, "Enter new limit (0 for no limit): ");
                wrefresh(main_win);
                // 这里怎么做的输入？
                echo();
                wgetnstr(main_win, limit_str, sizeof(limit_str) - 1);
                noecho();
                speed_limit = atoi(limit_str);
                if (speed_limit < 0 || speed_limit > 100) {
                    speed_limit = -1;
                }
                mvwprintw(main_win, 6, 1, "Mode:    %s \t\t\t\tLimit: %s", get_mode_string(current_mode), speed_limit == -1 ? "nolimit":limit_str);
            }
            break;
    }

    mvwprintw(main_win, 6, 1, "Mode: %s \t\t\t\tLimit: %s", get_mode_string(current_mode), speed_limit == -1 ? "nolimit":"limited");

    if (selected_option == 0) {
        wattron(main_win, A_REVERSE);
        mvwprintw(main_win, 6, 1, "Mode:    %s", get_mode_string(current_mode));
        wattroff(main_win, A_REVERSE);
    } else {
        mvwprintw(main_win, 6, 1, "Mode:    %s", get_mode_string(current_mode));
    }

    if (selected_option == 1) {
        wattron(main_win, A_REVERSE);
        mvwprintw(main_win, 7, 1, "Limit: %s", speed_limit == -1 ? "no limit" : limit_str);
        wattroff(main_win, A_REVERSE);
    } else {
        mvwprintw(main_win, 7, 1, "Limit: %s", speed_limit == -1 ? "no limit" : limit_str);
    }

    wrefresh(main_win);
}

int main(int argc, char * argv[]) {
    int server_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    pthread_t threads[MAX_CLIENTS];

    memset(clients, 0, sizeof(clients));

    if (argc < 5) {
        fprintf(stderr, "Usage: %s -p <tcp/udp> -m <up/down/double>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    char *protocol = NULL;
    char *mode = NULL;

    for (int i = 1; i < argc; i ++) {
        if (strcmp(argv[i], "-p") == 0) {
            protocol = argv[++i];
        } else if (strcmp(argv[i], "-m") == 0) {
            mode = argv[++i];
        }
    }

    if (protocol == NULL || mode == NULL) {
        fprintf(stderr, "Usage: %s -p <tcp/udp> -m <up/down/double>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // 这里只考虑了udp？
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

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    timeout(100);

    main_win = newwin(30, 80, 0, 0);
    box(main_win, 0, 0);
    mvwprintw(main_win, 1, 1, "Bandwidth Test Server");
    mvwprintw(main_win, 6, 1, "Mode:    %s \t\t\t\tLimit: %s", get_mode_string(current_mode), speed_limit == -1 ? "no limit" : "limited");
    mvwprintw(main_win, 8, 1, "Clients:");
    mvwprintw(main_win,  9, 1, "| Rank |  IP Address   |  Port  |       UP      |     DOWN      |");
    mvwprintw(main_win, 10, 1, "|------|---------------|--------|---------------|---------------|");
    wrefresh(main_win);

    server_fd = socket(AF_INET, is_udp ? SOCK_DGRAM:SOCK_STREAM, 0);

    if (server_fd < 0) {
        perror("socket");
        endwin();
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SERVER_PORT);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(server_fd);
        endwin();
        exit(EXIT_FAILURE);
    }

    if (!is_udp) {
        if (listen(server_fd, MAX_CLIENTS) < 0) {
            perror("listen");
            close(server_fd);
            endwin();
            exit(EXIT_FAILURE);
        }
    }

    signal(SIGALRM, handle_alarm);
    struct itimerval timer;
    timer.it_interval.tv_sec = 1;
    timer.it_interval.tv_usec = 0;
    timer.it_value.tv_sec = 1;
    timer.it_value.tv_usec = 0;
    setitimer(ITIMER_REAL, &timer, NULL);

    int i;
    for (int i = 0; i <MAX_CLIENTS; i ++) {
        pthread_mutex_init(&clients[i].lock, NULL);
    }

    while (1) {
        int client_fd;
        if (is_udp) {
            char buffer[BUFFER_SIZE];
            client_fd = recvfrom(server_fd, buffer, BUFFER_SIZE, 0, (struct sockaddr*)&client_addr, &client_addr_len);
        } else {
            client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_addr_len);
            if (client_fd < 0) {
                perror("accept");
                continue;
            }
        }

        for (i = 0; i < MAX_CLIENTS; i++) {
            if (!clients[i].active) {
                pthread_mutex_lock(&clients[i].lock);
                clients[i].fd = client_fd;
                clients[i].total_bytes_up = 0;
                clients[i].total_bytes_down = 0;

                gettimeofday(&clients[i].start, NULL);

                inet_ntop(AF_INET, &client_addr.sin_addr, clients[i].ip, sizeof(clients[i].ip));
                clients[i].port = ntohs(client_addr.sin_port);
                clients[i].active = 1;
                pthread_mutex_unlock(&clients[i].lock);

                pthread_create(&threads[i], NULL, handle_client, &clients[i]);
                break;
            }
        }

        if (i == MAX_CLIENTS) {
            close(client_fd);
        }

        int ch = getch();
        if (ch != ERR) {
            handle_keyboard_event(ch);
        }
    }

    for (i = 0; i < MAX_CLIENTS; i++) {
        pthread_mutex_destroy(&clients[i].lock);
    }

    close(server_fd);
    endwin();
    return 0;
}