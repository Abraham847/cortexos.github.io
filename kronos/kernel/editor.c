#include "kernel.h"
#include "fs.h"
#include "lang.h"

#define EBUF 4096
static char e_buf[EBUF];
static int e_len, e_pos, e_scroll, e_path_len, e_dirty, e_win_id;
static char e_path[32];

void edit_open(const char *fn) {
    e_win_id = wm_create(40, 30, 240, 140, tr(S_EDITOR), edit_draw, edit_keypress, 0);
    e_path_len = 0;
    if (fn) {
        int i;
        for (i = 0; fn[i] && i < 31; i++) e_path[i] = fn[i];
        e_path_len = i;
    }
}

void edit_init(void) {
    e_len = 0; e_pos = 0; e_scroll = 0; e_dirty = 0; e_path_len = 0; e_win_id = -1;
}

static int e_line_count(void) {
    int n = 1;
    for (int i = 0; i < e_len; i++) if (e_buf[i] == '\n') n++;
    return n;
}

static int e_line_start(int line) {
    int pos = 0, cl = 0;
    while (pos < e_len && cl < line) { if (e_buf[pos] == '\n') cl++; pos++; }
    return pos;
}

static int e_cur_line(void) {
    int n = 0;
    for (int i = 0; i < e_pos; i++) if (e_buf[i] == '\n') n++;
    return n;
}

void edit_draw(int id) {
    window_t *win = wm_get(id);
    if (!win) return;
    int bx = win->x + 35, by = win->y + 13, mx = win->x + win->w - 2;
    int my = win->y + win->h - 2;

    vga_fillrect(win->x + 1, win->y + 11, win->w - 2, win->h - 12, 1);
    vga_drawstring(win->x + 3, win->y + 11, tr(S_EDIT), 8, 1);

    char pbuf[20]; int pi = 0;
    for (int i = 0; i < e_path_len && pi < 18; i++) pbuf[pi++] = e_path[i];
    pbuf[pi] = 0;
    if (pi) vga_drawstring(win->x + 35, win->y + 11, pbuf, 7, 1);
    if (e_dirty) vga_drawchar(win->x + win->w - 18, win->y + 11, '*', 10, 1);

    int vis_lines = (my - by) / 9;
    if (vis_lines < 1) vis_lines = 1;

    for (int l = 0; l < vis_lines; l++) {
        int line = e_scroll + l;
        if (line >= e_line_count()) break;
        int s = e_line_start(line);
        int e = s;
        while (e < e_len && e_buf[e] != '\n') e++;
        int ly = by + l * 9;
        char num[8]; itoa(line + 1, num);
        vga_drawstring(win->x + 2, ly, num, 6, 1);
        int cx = bx;
        for (int i = s; i < e && cx < mx; i++) { vga_drawchar(cx, ly, e_buf[i], 7, 1); cx += 9; }
    }

    int cl = e_cur_line() - e_scroll;
    int cc = e_pos - e_line_start(e_cur_line());
    if (cl >= 0 && cl < vis_lines) {
        static int blink;
        if (++blink > 30) blink = 0;
        if (blink < 15) vga_drawchar(bx + cc * 9, by + cl * 9, '_', 11, 1);
    }
}

void edit_keypress(int id, char c) {
    (void)id;
        if (c == 0x85) {

        if (e_path_len > 0) {
            e_path[e_path_len] = 0;
            if (fs_write((const char*)e_path, (const u8*)e_buf, e_len) == e_len) e_dirty = 0;
        }
        return;
    }
    if (c == 0x86) {

        if (e_path_len > 0) {
            e_path[e_path_len] = 0;
            int n = fs_read((const char*)e_path, (u8*)e_buf, EBUF - 1);
            if (n > 0) { e_len = n; e_pos = 0; e_scroll = 0; e_dirty = 0; }
        }
        return;
    }
    if (c == 0x87) {

        wm_close(e_win_id);
        return;
    }
    if (c == 0x82 && e_pos > 0) e_pos--;
    if (c == 0x83 && e_pos < e_len) e_pos++;
    if (c == 0x80) {
        int cl = e_cur_line();
        if (cl > 0) {
            int cc = e_pos - e_line_start(cl);
            e_pos = e_line_start(cl - 1);
            int el = e_line_start(cl);
            while (e_pos < el && e_pos - e_line_start(cl - 1) < cc) e_pos++;
            if (e_pos > el) e_pos = el;
        }
    }
    if (c == 0x81) {

        int cl = e_cur_line(), tl = e_line_count() - 1;
        if (cl < tl) {
            int cc = e_pos - e_line_start(cl);
            e_pos = e_line_start(cl + 1);
            int el = e_len;
            int nl = e_line_start(cl + 2);
            if (nl > 0) el = nl - 1;
            while (e_pos < el && e_pos - e_line_start(cl + 1) < cc) e_pos++;
            if (e_pos > el) e_pos = el;
        }
    }
    if (c == 8 && e_pos > 0) {
        for (int i = e_pos - 1; i < e_len; i++) e_buf[i] = e_buf[i + 1];
        e_len--; e_pos--; e_dirty = 1;
    }
    if (c == 13 && e_len < EBUF - 1) {
        for (int i = e_len; i > e_pos; i--) e_buf[i] = e_buf[i - 1];
        e_buf[e_pos] = '\n'; e_len++; e_pos++; e_dirty = 1;
    }
    if (c >= 32 && e_len < EBUF - 1) {
        for (int i = e_len; i > e_pos; i--) e_buf[i] = e_buf[i - 1];
        e_buf[e_pos] = c; e_len++; e_pos++; e_dirty = 1;
    }
    /* scroll to keep cursor visible */
    int cl = e_cur_line();
    if (cl < e_scroll) e_scroll = cl;
    if (cl >= e_scroll + 10) e_scroll = cl - 9;
}
