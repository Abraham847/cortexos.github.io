#include "kernel.h"
#include "lang.h"

#define BTN_N 6
static const char *btn_labels[BTN_N] = {"Train", "Editor", "Dataset", "Weights", "Demo", "Infer"};
static int btn_sel;

static char info_buf[128];
static int info_dirty;

void ailab_init(void) {
    btn_sel = -1;
    info_dirty = 1;
}

void ailab_draw(int id) {
    window_t *win = wm_get(id);
    if (!win) return;
    int bx = win->x, by = win->y;
    int w = win->w, h = win->h;

    vga_fillrect(bx, by, w, h, 1);

    vga_drawstring(bx + 4, by + 4, "AI Lab v2.0", 14, 1);
    vga_drawrect(bx + 4, by + 16, w - 8, 1, 8);

    int bw = 60, bh = 20, gap = 5;
    int start_x = bx + 4;
    int start_y = by + 22;
    for (int i = 0; i < BTN_N; i++) {
        int col = i % 3;
        int row = i / 3;
        int cx = start_x + col * (bw + gap);
        int cy = start_y + row * (bh + gap);
        u8 fg = btn_sel == i ? 15 : 7;
        u8 bg = btn_sel == i ? 4 : 1;
        vga_drawrect(cx, cy, bw, bh, fg);
        vga_fillrect(cx + 1, cy + 1, bw - 2, bh - 2, bg);
        vga_drawstring(cx + 4, cy + 4, btn_labels[i], fg, bg);
    }

    int sy = start_y + 2 * (bh + gap) + 6;
    vga_drawrect(bx + 4, sy, w - 8, 1, 8);
    vga_drawstring(bx + 4, sy + 4, "Active:", 10, 1);

    int line = 0;
    for (int si = 0; si < ai_get_slot_count(); si++) {
        if (ai_slot_ready(si)) {
            char buf[32];
            vga_drawstring(bx + 8, sy + 16 + line * 10, "Slot ", 7, 1);
            itoa(si, buf);
            vga_drawstring(bx + 38, sy + 16 + line * 10, buf, 11, 1);
            vga_drawstring(bx + 50, sy + 16 + line * 10, "ready", 8, 1);
            line++;
        }
    }

    vga_drawrect(bx + 4, sy + 16 + line * 10 + 4, w - 8, 1, 8);

    vga_drawstring(bx + 4, sy + 16 + line * 10 + 10, "Slot: [1][2][3][4]", 7, 1);
    vga_drawstring(bx + 4, sy + 16 + line * 10 + 22, "Arrows:select  Enter:launch", 8, 1);
    vga_drawstring(bx + 4, sy + 16 + line * 10 + 34, "L:load  S:save  D:dataset", 8, 1);
}

void ailab_keypress(int id, char c) {
    if (c == 0x81) { btn_sel = (btn_sel + 1) % BTN_N; return; }
    if (c == 0x80) { btn_sel = (btn_sel - 1 + BTN_N) % BTN_N; return; }
    if (c == 13 || c == ' ') {
        if (btn_sel < 0) btn_sel = 0;
        switch (btn_sel) {
            case 0: ai_open_trainer(0); break;
            case 1: ai_open_editor(0); break;
            case 2: ai_open_ds_view(); break;
            case 3: ai_open_weights(0); break;
            case 4: sysmon_open(); break;
            case 5: wm_create(40, 40, 160, 120, "Infer", ai_draw, ai_keypress, 0); break;
        }
    }
    if (c >= '1' && c <= '4') {
        int si = c - '1';
        ai_select_slot(si);
        btn_sel = si;
    }
    if (c == 'l' || c == 'L') {
        file_t f; char name[] = "MODEL.SYS";
        vga_drawstring(10, 10, "Load model...", 7, 1);
    }
    if (c == 's' || c == 'S') {
        ai_save_model("MODEL.SYS");
    }
    if (c == 'd' || c == 'D') {
        ai_open_ds_view();
    }
}

void ailab_click(int id, int mx, int my) {
    window_t *win = wm_get(id);
    if (!win) return;
    int bw = 60, bh = 20, gap = 5;
    int start_x = win->x + 4;
    int start_y = win->y + 22;
    for (int i = 0; i < BTN_N; i++) {
        int col = i % 3;
        int row = i / 3;
        int cx = start_x + col * (bw + gap);
        int cy = start_y + row * (bh + gap);
        if (mx >= cx && mx < cx + bw && my >= cy && my < cy + bh) {
            btn_sel = i;
            char c = 13;
            ailab_keypress(id, c);
            return;
        }
    }
}
