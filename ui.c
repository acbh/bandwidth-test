#include "ui.h"

WINDOW *main_win;

const char* get_mode_string(test_mode_t mode) {
    switch (mode) {
        case MODE_DOUBLE: return "DOUBLE";
        case MODE_UP: return "UP";
        case MODE_DOWN: return "DOWN";
        default: return "UNKNOWN";
    }
}

void update_interface() {
    mvwprintw(main_win, 4, 1, "Mode:    %s \t\t\t\tLimit: no limit", get_mode_string(current_mode));
    mvwprintw(main_win, 8, 1, "| RANK | IP\t\t|  PORT  | UP\t\t | DOWN\t\t |");
    mvwprintw(main_win, 9, 1, "-----------------------------------------------------------------");
    wrefresh(main_win);
}

void init_ncurses() {
    initscr();
    cbreak();
    noecho();
    curs_set(0);
    mousemask(ALL_MOUSE_EVENTS, NULL);

    main_win = newwin(MAX_CLIENTS * 2 + 4, 80, 0, 0);
    box(main_win, 0, 0);
    update_interface();
}

void handle_input(int ch) {
    MEVENT event;
    if (ch == KEY_MOUSE) {
        if (getmouse(&event) == OK) {
            if (event.y == 4 && event.x >= 7 && event.x <= 13) {
                switch (current_mode) {
                    case MODE_DOUBLE: current_mode = MODE_UP; break;
                    case MODE_UP: current_mode = MODE_DOWN; break;
                    case MODE_DOWN: current_mode = MODE_DOUBLE; break;
                }
                update_interface();
            }
        }
    }
}
