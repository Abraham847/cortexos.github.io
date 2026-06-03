#include "kernel.h"
#include "lang.h"
#include "nn.h"
#include "ai.h"

#define NNV_MAX_IN 8
#define NNV_MAX_OUT 4
#define NNV_INPUTS 2
#define NNV_OUTPUTS 1

static int nnv_win_id = -1;
static int nnv_inputs[NNV_MAX_IN];
static fp nnv_outputs[NNV_MAX_OUT];
static nn nnv_net;
static int nnv_ready;
static int nnv_mode;
static int nnv_sel;

static const int nnv_xor[4][3] = {{0,0,0},{0,1,1},{1,0,1},{1,1,0}};
static fp nnv_xor_fp[4][3];
static int nnv_example;

static dataset nnv_ds;

static void nnv_train(void) {
    if (nnv_ready && nnv_ds.n > 0) {
        for (int e = 0; e < 200; e++)
            ds_train_epoch(&nnv_net, &nnv_ds, FF(0.5));
    }
}

static void nnv_build_net(void) {
    if (nnv_ready) nn_free(&nnv_net);
    nnv_ready = 0;
    int sz[] = {NNV_INPUTS, 6, NNV_OUTPUTS};
    if (nn_init(&nnv_net, 3, sz)) return;
    nn_rand(&nnv_net, FF(2));
    nnv_ready = 1;
    nnv_train();
}

static void nnv_run(void) {
    if (!nnv_ready) return;
    fp in[NNV_MAX_IN];
    for (int i = 0; i < NNV_INPUTS; i++) in[i] = nnv_inputs[i] ? F1 : 0;
    nn_fwd(&nnv_net, in);
    for (int i = 0; i < NNV_OUTPUTS; i++)
        nnv_outputs[i] = nnv_net.a[nnv_net.nl - 1].d[i];
}

static void nnv_load_example(int ex) {
    nnv_example = ex;
    for (int i = 0; i < NNV_INPUTS; i++)
        nnv_inputs[i] = nnv_xor[ex][i];
    nnv_run();
}

void nnview_init(void) {
    nnv_ready = 0;
    nnv_win_id = -1;
    nnv_sel = 0;
    nnv_example = 0;
    for (int i = 0; i < NNV_INPUTS; i++) nnv_inputs[i] = 0;
    for (int i = 0; i < NNV_OUTPUTS; i++) nnv_outputs[i] = 0;
    for (int i = 0; i < 4; i++) {
        nnv_xor_fp[i][0] = FF(nnv_xor[i][0]);
        nnv_xor_fp[i][1] = FF(nnv_xor[i][1]);
        nnv_xor_fp[i][2] = FF(nnv_xor[i][2]);
    }
    nnv_ds.n = 4;
    nnv_ds.ni = 2;
    nnv_ds.no = 1;
    nnv_ds.in = &nnv_xor_fp[0][0];
    nnv_ds.out = &nnv_xor_fp[0][2];
    nnv_build_net();
    nnv_load_example(0);
}

static int nnv_get_output_pct(int i) {
    if (!nnv_ready || i >= NNV_OUTPUTS) return 0;
    int v = FI((long long)nnv_outputs[i] * 100 / F1);
    if (v > 100) v = 100; if (v < 0) v = 0;
    return v;
}

void nnview_draw(int id) {
    window_t *win = wm_get(id);
    if (!win) return;
    int bx = win->x, by = win->y;

    if (!nnv_ready) {
        vga_drawstring(bx + 5, by + 15, "No network", 12, 1);
        return;
    }

    vga_fillrect(bx, by, win->w, win->h, 1);

    int cx = bx + win->w / 2;
    int ncol_w = win->w / 3;

    int title_y = by + 5;
    vga_drawstring(bx + 5, title_y, "NN Viewer", 11, 1);
    vga_drawstring(bx + win->w - 60, title_y, "[Q]uit", 7, 1);

    int net_y = by + 16;
    int lyr_y[3] = {net_y, net_y, net_y};
    int lyr_x[3] = {bx + ncol_w / 2, bx + ncol_w + ncol_w / 2, bx + ncol_w * 2 + ncol_w / 2};
    int sz[3] = {NNV_INPUTS, nnv_net.sz[1], NNV_OUTPUTS};
    int ny[3][NNV_MAX_IN];

    vga_drawstring(bx + ncol_w / 2 - lyr_x[0] + bx + ncol_w / 2 - 8, net_y, "IN", 8, 1);
    vga_drawstring(bx + ncol_w + ncol_w / 2 - 8, net_y, "HID", 8, 1);
    vga_drawstring(bx + ncol_w * 2 + ncol_w / 2 - 12, net_y, "OUT", 8, 1);

    for (int l = 0; l < 3; l++) {
        int y0 = net_y + 14;
        int sp = win->h - 40;
        if (sz[l] > 1) sp = sp / (sz[l] - 1); else sp = 30;
        if (sp > 30) sp = 30;
        int start_y = y0 + ((win->h - 40) - (sz[l] - 1) * sp) / 2;
        for (int n = 0; n < sz[l]; n++) {
            ny[l][n] = start_y + n * sp;
        }
    }

    for (int l = 0; l < 2; l++)
        for (int n = 0; n < sz[l]; n++)
            for (int m = 0; m < sz[l + 1]; m++) {
                fp w = nnv_net.w[l].d[m * nnv_net.sz[l] + n];
                u8 col = (w > 0) ? 11 : 5;
                for (int t = 0; t < 8; t++) {
                    int px = lyr_x[l] + (lyr_x[l + 1] - lyr_x[l]) * t / 7;
                    int py = ny[l][n] + (ny[l + 1][m] - ny[l][n]) * t / 7;
                    vga_putpixel(px, py, col);
                }
            }

    for (int n = 0; n < sz[0]; n++) {
        int col = nnv_inputs[n] ? 14 : 4;
        vga_drawcircle(lyr_x[0], ny[0][n], 5, col);
        vga_drawcircle(lyr_x[0], ny[0][n], 4, 0);
        vga_drawcircle(lyr_x[0], ny[0][n], 3, n == nnv_sel && nnv_mode == 0 ? 15 : col);
        char lb[2] = {'0' + nnv_inputs[n], 0};
        vga_drawstring(lyr_x[0] + 8, ny[0][n] - 3, lb, col, 1);
    }

    for (int n = 0; n < sz[1]; n++) {
        fp v = nnv_net.a[1].d[n];
        int vi = FI(v * 14);
        if (vi > 14) vi = 14; if (vi < 1) vi = 1;
        vga_drawcircle(lyr_x[1], ny[1][n], 4, vi + 1);
        vga_drawcircle(lyr_x[1], ny[1][n], 3, 0);
        vga_drawcircle(lyr_x[1], ny[1][n], 2, vi + 1);
    }

    for (int n = 0; n < sz[2]; n++) {
        int pct = nnv_get_output_pct(n);
        int col = pct > 50 ? 10 : 4;
        vga_drawcircle(lyr_x[2], ny[2][n], 5, col);
        vga_drawcircle(lyr_x[2], ny[2][n], 4, 0);
        vga_drawcircle(lyr_x[2], ny[2][n], 3, col);
        char ob[4]; itoa(pct, ob);
        vga_drawstring(lyr_x[2] + 8, ny[2][n] - 3, ob, col, 1);
        vga_drawstring(lyr_x[2] + 8 + 12, ny[2][n] - 3, "%", col, 1);
    }

    int row = win->y + win->h - 35;
    vga_drawstring(bx + 5, row, "[0/1] inp [< >] ex", 7, 1);
    row += 9;
    vga_drawstring(bx + 5, row, "[R]nd [C]lr [S]ave [A]nd [O]r [X]or", 7, 1);

    if (nnv_mode == 0) {
        vga_drawchar(bx + ncol_w / 2 - 6, ny[0][nnv_sel] - 7, '@', 15, 1);
    }
}

void nnview_keypress(int id, char c) {
    if (c == 'q' || c == 'Q') { wm_close(id); return; }
    if (c == 'r' || c == 'R') { nnv_build_net(); nnv_run(); return; }
    if (c == 'c' || c == 'C') { for (int i = 0; i < NNV_INPUTS; i++) nnv_inputs[i] = 0; nnv_run(); return; }
    if (c == 's' || c == 'S') { if (nnv_ready) { nn_export_csv_file(&nnv_net, "NNVIEW.CSV"); } return; }
    if (c == 'x' || c == 'X') { nnv_load_example(0); return; }
    if (c == 'a' || c == 'A') { nnv_load_example(3); return; }
    if (c == 'o' || c == 'O') { nnv_load_example(2); return; }
    if (c == '0') { nnv_inputs[0] = 0; nnv_run(); return; }
    if (c == '1') { nnv_inputs[0] = 1; nnv_run(); return; }
    if (c == 0x83) { nnv_example = (nnv_example + 1) % 4; nnv_load_example(nnv_example); return; }
    if (c == 0x82) { nnv_example = (nnv_example - 1 + 4) % 4; nnv_load_example(nnv_example); return; }
    if (c == 0x80 || c == 0x81) {
        nnv_sel = (nnv_sel + 1) % NNV_INPUTS;
        return;
    }
}

void nnview_click(int id, int mx, int my) {
    window_t *win = wm_get(id);
    if (!win || !nnv_ready) return;
    int bx = win->x, by = win->y;
    int ncol_w = win->w / 3;
    int lyr_x0 = bx + ncol_w / 2;
    int net_y = by + 16;
    int y0 = net_y + 14;
    int sp = win->h - 40;
    if (NNV_INPUTS > 1) sp = sp / (NNV_INPUTS - 1); else sp = 30;
    if (sp > 30) sp = 30;
    int start_y = y0 + ((win->h - 40) - (NNV_INPUTS - 1) * sp) / 2;
    for (int n = 0; n < NNV_INPUTS; n++) {
        int nx = lyr_x0;
        int ny = start_y + n * sp;
        if (mx >= nx - 8 && mx <= nx + 16 && my >= ny - 8 && my <= ny + 8) {
            nnv_inputs[n] = !nnv_inputs[n];
            nnv_run();
            nnv_sel = n;
            nnv_mode = 0;
            return;
        }
    }
}

void nnview_open(void) {
    if (nnv_win_id >= 0) {
        window_t *w = wm_get(nnv_win_id);
        if (w && w->visible) { wm_focus(nnv_win_id); return; }
    }
    nnv_build_net();
    nnv_load_example(0);
    nnv_win_id = wm_create(30, 30, 250, 175, "NN Viewer", nnview_draw, nnview_keypress, nnview_click);
}
