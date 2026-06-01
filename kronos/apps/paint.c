#include "app_api.h"

static const app_api_t *A;

/* lang.h: S_PAINT = 21 */
#define SI_PAINT 21

static int win_id, color, brush_size;

static void draw(int id) {
    (void)id;
    window_t *win = A->wm_get(win_id);
    if (!win) return;
    int bx = win->x, by = win->y;
    A->drawstring(bx + 3, by + 12, A->tr(SI_PAINT), 7, 1);
    int cx = bx + 55;
    for (int i = 1; i < 16; i++) {
        A->fillrect(cx, by + 12, 8, 8, i);
        A->drawrect(cx, by + 12, 8, 8, i == color ? 15 : 0);
        cx += 10;
    }
}

static void keypress(int id, char c) {
    (void)id;
    if (c >= '1' && c <= '9') color = c - '0';
    if (c == '0') color = 10;
    if (c == '-' && brush_size > 1) brush_size--;
    if (c == '=' && brush_size < 8) brush_size++;
}

static void click(int id, int mx, int my) {
    window_t *win = A->wm_get(id);
    if (!win) return;
    if (my > 25) {
        int abs_x = win->x + mx;
        int abs_y = win->y + my;
        for (int dy = -brush_size; dy <= brush_size; dy++)
            for (int dx = -brush_size; dx <= brush_size; dx++)
                A->putpixel(abs_x + dx, abs_y + dy, color);
    } else if (mx > 45 && mx < 200 && my > 11 && my < 22) {
        int idx = (mx - 55) / 10 + 1;
        if (idx >= 1 && idx <= 15) color = idx;
    }
}

void entry(const app_api_t *api) {
    A = api;
    color = 7;
    brush_size = 2;
    win_id = A->wm_create(10, 10, 200, 140, A->tr(SI_PAINT), draw, keypress, click);
    if (win_id < 0) win_id = -1;
}
