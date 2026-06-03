#ifndef WINDOW_H
#define WINDOW_H

#include "core.h"

typedef struct {
    int x, y, w, h;
    char title[24];
    int active, visible, dirty;
    void (*draw)(int id);
    void (*keypress)(int id, char c);
    void (*click)(int id, int mx, int my);
} window_t;

#define MAX_WINS 12

void wm_init(void);
int wm_create(int x, int y, int w, int h, const char *title,
              void (*draw)(int), void (*keypress)(int, char), void (*click)(int, int, int));
void wm_close(int id);
void wm_draw(void);
void wm_handle_click(int mx, int my);
void wm_drag_move(int mx, int my);
void wm_handle_key(char c);
void wm_focus(int id);
window_t *wm_get(int id);

#endif
