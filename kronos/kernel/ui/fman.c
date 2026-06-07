#include "kernel.h"
#include "fman.h"
#include "fs.h"
#include "lang.h"

static fs_entry_t fman_ents[50];
static int fman_n;
static int fman_sel;
static int fman_top;
static int fman_dirty = 1;

static void fman_refresh(void) {
    fman_n = fs_list("", fman_ents, 50);
    fman_dirty = 1;
}

void fman_open(void) {
    fman_sel = 0;
    fman_top = 0;
    fman_refresh();
    wm_create(60, 50, 200, 150, "Files", fman_draw, fman_keypress, fman_click);
}

void fman_draw(int id) {
    if (!fman_dirty) return;
    fman_dirty = 0;
    window_t *win = wm_get(id);
    if (!win) return;
    int bx = win->x, by = win->y;
    int y = by + 15;
    int visible = (win->h - 35) / 10;
    vga_drawstring(bx + 3, by + 14, "Files", 5, 1);
    for (int i = 0; i < visible && fman_top + i < fman_n; i++) {
        int idx = fman_top + i;
        u8 fg = (idx == fman_sel) ? 15 : 7;
        u8 bg = (idx == fman_sel) ? 5 : 1;
        vga_drawstring(bx + 3, y, fman_ents[idx].name, fg, bg);
        char sb[16];
        itoa(fman_ents[idx].size, sb);
        vga_drawstring(bx + 110, y, sb, fg, bg);
        y += 10;
    }
    char buf[8];
    itoa(fman_sel + 1, buf);
    vga_drawstring(bx + 3, win->y + win->h - 14, buf, 8, 1);
    vga_drawstring(bx + 20, win->y + win->h - 14, "/", 8, 1);
    itoa(fman_n, buf);
    vga_drawstring(bx + 25, win->y + win->h - 14, buf, 8, 1);
}

void fman_keypress(int id, char c) {
    (void)id;
    if (c == 0x81 && fman_sel < fman_n - 1) {
        fman_sel++;
        if (fman_sel >= fman_top + 8) fman_top = fman_sel - 7;
        fman_dirty = 1;
    }
    if (c == 0x80 && fman_sel > 0) {
        fman_sel--;
        if (fman_sel < fman_top) fman_top = fman_sel;
        fman_dirty = 1;
    }
    if (c == 13 && fman_n > 0) {
        char *name = fman_ents[fman_sel].name;
        extern void edit_open(const char *fn);
        edit_open(name);
    }
    if (c == 'd' || c == 'D') {
        if (fman_n > 0) {
            fs_delete(fman_ents[fman_sel].name);
            fman_refresh();
            if (fman_sel >= fman_n) fman_sel = fman_n - 1;
            if (fman_sel < 0) fman_sel = 0;
        }
    }
}

void fman_click(int id, int mx, int my) {
    (void)id;
    int idx = (my - 15) / 10 + fman_top;
    if (idx >= 0 && idx < fman_n) {
        fman_sel = idx;
        fman_dirty = 1;
    }
}
