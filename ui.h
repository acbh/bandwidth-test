#ifndef UI_H
#define UI_H

#include <ncurses.h>
#include "server.h"

void init_ncurses();
void update_interface();
void handle_input(int ch);

#endif // UI_H
