#include "kernel.h"
#include "lang.h"

static int paint_win_id, paint_color, paint_size;

void paint_init(void) {
    paint_win_id = -1; paint_color = 7; paint_size = 2;
}

void paint_open(void) {
    paint_win_id = wm_create(10, 10, 200, 140, tr(S_PAINT), paint_draw, paint_keypress, paint_click);
}

void paint_draw(int id) {
    window_t *win = wm_get(id);
    if (!win) return;
    int bx = win->x, by = win->y;
    vga_drawstring(bx + 3, by + 12, tr(S_COLORS), 7, 1);
    int cx = bx + 55;
    for (int i = 1; i < 16; i++) {
        vga_fillrect(cx, by + 12, 8, 8, i);
        vga_drawrect(cx, by + 12, 8, 8, i == paint_color ? 15 : 0);
        cx += 10;
    }
    vga_drawstring(bx + 3, by + 22, tr(S_DRAG_DRAW), 8, 1);
}

void paint_keypress(int id, char c) {
    (void)id;
    if (c >= '1' && c <= '9') paint_color = c - '0';
    if (c == '0') paint_color = 10;
    if (c == '-' && paint_size > 1) paint_size--;
    if (c == '=' && paint_size < 8) paint_size++;
}

void paint_click(int id, int mx, int my) {
    window_t *win = wm_get(id);
    if (!win) return;
    if (my > 25) {
        int abs_x = win->x + mx;
        int abs_y = win->y + my;
        for (int dy = -paint_size; dy <= paint_size; dy++)
            for (int dx = -paint_size; dx <= paint_size; dx++)
                vga_putpixel(abs_x + dx, abs_y + dy, paint_color);
    } else if (mx > 45 && mx < 200 && my > 11 && my < 22) {
        int idx = (mx - 55) / 10 + 1;
        if (idx >= 1 && idx <= 15) paint_color = idx;
    }
}
