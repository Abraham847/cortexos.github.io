#include "kernel.h"

static window_t wins[MAX_WINS];
static int win_count = 0;
static int focused = -1;

static u8 win_bg = 1, win_frame = 11, win_title = 7, win_title_bg = 6;
static int drag_win = -1, drag_ox, drag_oy;

void wm_init(void) {
    for (int i = 0; i < MAX_WINS; i++) wins[i].visible = 0;
    win_count = 0;
}

int wm_create(int x, int y, int w, int h, const char *title,
              void (*draw)(int), void (*keypress)(int, char), void (*click)(int, int, int)) {
    int id = -1;
    for (int i = 0; i < MAX_WINS; i++) {
        if (!wins[i].visible) { id = i; break; }
    }
    if (id < 0) return -1;
    if (id >= win_count) win_count = id + 1;
    window_t *win = &wins[id];
    win->x = x; win->y = y; win->w = w; win->h = h;
    int i;
    for (i = 0; title[i] && i < 23; i++) win->title[i] = title[i];
    win->title[i] = 0;
    win->active = 1; win->visible = 1;
    win->draw = draw; win->keypress = keypress; win->click = click;
    wm_focus(id);
    return id;
}

void wm_close(int id) {
    if (id >= 0 && id < win_count) wins[id].visible = 0;
}

void wm_focus(int id) {
    if (focused >= 0) wins[focused].active = 0;
    focused = id;
    if (id >= 0) wins[id].active = 1;
}

window_t *wm_get(int id) {
    if (id < 0 || id >= win_count) return 0;
    return &wins[id];
}

static void draw_title(window_t *win) {
    int tb = 10;
    vga_fillrect(win->x, win->y, win->w, tb, win->active ? 6 : 5);
    vga_drawrect(win->x, win->y, win->w, win->h, win->active ? 11 : 8);
    vga_drawstring(win->x + 3, win->y + 1, win->title, 15, win->active ? 6 : 5);
    vga_drawchar(win->x + win->w - 10, win->y + 1, 'X', 12, win->active ? 6 : 5);
}

void wm_draw(void) {
    for (int i = 0; i < win_count; i++) {
        if (!wins[i].visible) continue;
        window_t *win = &wins[i];
        vga_fillrect(win->x + 1, win->y + 11, win->w - 2, win->h - 12, 1);
        draw_title(win);
        if (win->draw) win->draw(i);
    }
}

void wm_handle_click(int mx, int my) {
    drag_win = -1;
    for (int i = win_count - 1; i >= 0; i--) {
        if (!wins[i].visible) continue;
        window_t *win = &wins[i];
        if (mx >= win->x && mx < win->x + win->w && my >= win->y && my < win->y + win->h) {
            wm_focus(i);
            if (my < win->y + 10) {
                if (mx >= win->x + win->w - 12 && mx < win->x + win->w - 2) {
                    wm_close(i); return;
                }
                drag_win = i;
                drag_ox = mx - win->x;
                drag_oy = my - win->y;
            } else if (win->click) {
                win->click(i, mx - win->x, my - win->y);
            }
            break;
        }
    }
}

void wm_handle_key(char c) {
    if (focused >= 0 && wins[focused].keypress)
        wins[focused].keypress(focused, c);
}

void wm_drag_move(int mx, int my) {
    if (drag_win >= 0) {
        wins[drag_win].x = mx - drag_ox;
        wins[drag_win].y = my - drag_oy;
        if (wins[drag_win].x < 0) wins[drag_win].x = 0;
        if (wins[drag_win].y < 0) wins[drag_win].y = 0;
        if (wins[drag_win].x + wins[drag_win].w > (int)vga_width) wins[drag_win].x = vga_width - wins[drag_win].w;
        if (wins[drag_win].y + wins[drag_win].h > (int)vga_height) wins[drag_win].y = vga_height - wins[drag_win].h;
    }
}
