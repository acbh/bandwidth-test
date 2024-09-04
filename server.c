#include "server.h"
#include "ui.h"
#include "client_handler.h"

int main() {
    struct sockaddr_in server_addr;
    int server_fd = init_server(&server_addr);
    pthread_t threads[MAX_CLIENTS];
    int thread_count = 0;

    memset(clients, 0, sizeof(clients));

    init_ncurses();

    struct itimerval timer;
    signal(SIGALRM, handle_alarm);

    timer.it_value.tv_sec = 1;
    timer.it_value.tv_usec = 0;
    timer.it_interval.tv_sec = 1;
    timer.it_interval.tv_usec = 0;

    setitimer(ITIMER_REAL, &timer, NULL);

    int ch;
    while ((ch = wgetch(main_win)) != 'q') {
        handle_input(ch);
    }

    close(server_fd);
    delwin(main_win);
    endwin();

    return 0;
}
