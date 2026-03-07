#ifndef MENU_H
#define MENU_H

#include <stdbool.h>

typedef void (*PageFunction)(void);

typedef struct {
    const char *name;
    PageFunction func;
} MenuItem;

void draw_menu(void);
void handle_button(int button);

#endif