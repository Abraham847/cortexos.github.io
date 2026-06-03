#include "kernel.h"
#include "lang.h"

#define PANEL_DS 0
#define PANEL_MODEL 1
#define PANEL_TRAIN 2
#define PANEL_INFER 3
#define PANEL_EXPORT 4

static int panel;
static int prev_panel;

static int ds_ni, ds_no, ds_count;
static char ds_path[32];

static int model_layers, model_sizes[NN_MAX_L], model_act;
static char model_path[32];

static int train_epochs, train_lr, train_running, train_progress;

static char infer_input[64];
static int infer_pos;

static int status_line;

static void ds_panel(int bx, int by, int w, int h) {
    int y = by + 4;
    vga_drawstring(bx + 4, y, "=== Dataset ===", 14, 1); y += 12;
    vga_drawstring(bx + 4, y, "Inputs:", 7, 1);
    char db[16]; itoa(ds_ni, db);
    vga_drawstring(bx + 60, y, db, 11, 1); y += 10;
    vga_drawstring(bx + 4, y, "Outputs:", 7, 1);
    itoa(ds_no, db);
    vga_drawstring(bx + 60, y, db, 11, 1); y += 10;
    vga_drawstring(bx + 4, y, "Samples:", 7, 1);
    itoa(ds_count, db);
    vga_drawstring(bx + 60, y, db, 11, 1); y += 10;
    vga_drawstring(bx + 4, y, "File:", 7, 1);
    vga_drawstring(bx + 40, y, ds_path[0] ? ds_path : "(none)", 8, 1); y += 12;
    vga_drawstring(bx + 4, y, "[N]ew  [L]oad  [A]dd sample", 10, 1); y += 10;
    vga_drawstring(bx + 4, y, "[S]ave  [C]lear  [V]iew", 10, 1);
}

static void model_panel(int bx, int by, int w, int h) {
    int y = by + 4;
    vga_drawstring(bx + 4, y, "=== Model ===", 14, 1); y += 12;
    vga_drawstring(bx + 4, y, "Layers:", 7, 1);
    char db[16]; itoa(model_layers, db);
    vga_drawstring(bx + 50, y, db, 11, 1); y += 10;
    vga_drawstring(bx + 4, y, "Arch: ", 7, 1);
    int px = bx + 40;
    for (int i = 0; i < model_layers; i++) {
        itoa(model_sizes[i], db);
        vga_drawstring(px, y, db, 11, 1);
        px += strlen(db) * 9 + 4;
        if (i < model_layers - 1) vga_drawstring(px - 4, y, "-", 8, 1);
    }
    y += 10;
    const char *acts = model_act == ACT_SIGMOID ? "Sigmoid" : (model_act == ACT_RELU ? "ReLU" : "Tanh");
    vga_drawstring(bx + 4, y, "Act:", 7, 1);
    vga_drawstring(bx + 40, y, acts, 11, 1); y += 10;
    vga_drawstring(bx + 4, y, "File:", 7, 1);
    vga_drawstring(bx + 40, y, model_path[0] ? model_path : "(none)", 8, 1); y += 12;
    vga_drawstring(bx + 4, y, "[C]reate  [L]oad  [S]ave", 10, 1); y += 10;
    vga_drawstring(bx + 4, y, "[A]uto train [1-4] slot", 10, 1);
}

static void train_panel(int bx, int by, int w, int h) {
    int y = by + 4;
    vga_drawstring(bx + 4, y, "=== Training ===", 14, 1); y += 12;
    char db[16];
    itoa(train_epochs, db);
    vga_drawstring(bx + 4, y, "Epochs:", 7, 1);
    vga_drawstring(bx + 50, y, db, 11, 1); y += 10;
    itoa(train_lr, db);
    vga_drawstring(bx + 4, y, "LR:", 7, 1);
    vga_drawstring(bx + 50, y, db, 11, 1); y += 10;
    if (train_running) {
        vga_drawstring(bx + 4, y, "Status: TRAINING", 10, 1); y += 10;
        itoa(train_progress, db);
        vga_drawstring(bx + 4, y, "Step: ", 7, 1);
        vga_drawstring(bx + 40, y, db, 11, 1); y += 10;
    } else if (ai_slot_ready(0) || ai_slot_ready(1) || ai_slot_ready(2) || ai_slot_ready(3)) {
        char ibuf[80]; ai_get_info(ibuf, 80);
        vga_drawstring(bx + 4, y, ibuf, 8, 1); y += 12;
    } else {
        vga_drawstring(bx + 4, y, "No model loaded", 8, 1); y += 10;
    }
    y += 4;
    vga_drawstring(bx + 4, y, "[T]rain  [S]top  [R]eset", 10, 1);
}

static void infer_panel(int bx, int by, int w, int h) {
    int y = by + 4;
    vga_drawstring(bx + 4, y, "=== Inference ===", 14, 1); y += 12;
    vga_drawstring(bx + 4, y, "Input:", 7, 1);
    vga_drawstring(bx + 40, y, infer_input, 11, 1);
    if (infer_pos < 60) vga_drawchar(bx + 40 + infer_pos * 9, y, '_', 11, 1);
    y += 12;
    if (infer_input[0]) {
        char result[64];
        int rn = ai_infer_str(infer_input, result, 64);
        if (rn > 0) {
            vga_drawstring(bx + 4, y, "Result:", 7, 1);
            vga_drawstring(bx + 50, y, result, 11, 1);
            y += 10;
        } else if (rn == -2) {
            vga_drawstring(bx + 4, y, "Bad input dims", 12, 1);
        }
    }
    y += 4;
    vga_drawstring(bx + 4, y, "[Enter] infer  [C]lear input", 10, 1);
}

static void export_panel(int bx, int by, int w, int h) {
    int y = by + 4;
    vga_drawstring(bx + 4, y, "=== Export ===", 14, 1); y += 12;
    vga_drawstring(bx + 4, y, "[T]ext: export as TXT", 10, 1); y += 10;
    vga_drawstring(bx + 4, y, "[C]ode: export as C header", 10, 1); y += 10;
    vga_drawstring(bx + 4, y, "[S]lot save  [L]oad slot", 10, 1);
}

void aistudio_init(void) {
    panel = PANEL_DS;
    ds_ni = 2; ds_no = 1; ds_count = 0; ds_path[0] = 0;
    model_layers = 3; model_sizes[0] = 2; model_sizes[1] = 4; model_sizes[2] = 1;
    model_act = ACT_SIGMOID; model_path[0] = 0;
    train_epochs = 100; train_lr = 50; train_running = 0; train_progress = 0;
    infer_input[0] = 0; infer_pos = 0;
    status_line = 0;
}

void aistudio_draw(int id) {
    window_t *win = wm_get(id);
    if (!win) return;
    int bx = win->x, by = win->y;
    int w = win->w, h = win->h;

    vga_fillrect(bx, by, w, h, 1);

    vga_drawrect(bx, by, w, h, 7);
    vga_drawstring(bx + 4, by + 2, "AI Studio v1.0", 14, 1);

    int tw = (w - 20) / 5;
    if (tw < 30) tw = 30;
    for (int i = 0; i < 5; i++) {
        int px = bx + 4 + i * (tw + 2);
        u8 fg = panel == i ? 15 : 7;
        u8 bg = panel == i ? 4 : 1;
        vga_drawrect(px, by + 14, tw, 10, fg);
        vga_fillrect(px + 1, by + 15, tw - 2, 8, bg);
        const char *labels[5] = {"Data", "Model", "Train", "Infer", "Export"};
        vga_drawstring(px + 2, by + 15, labels[i], fg, bg);
    }

    int cw = w - 12;
    int cy = by + 28;
    int ch = h - 42;

    vga_drawrect(bx + 4, cy, cw, ch, 6);
    vga_fillrect(bx + 5, cy + 1, cw - 2, ch - 2, 1);

    switch (panel) {
        case PANEL_DS: ds_panel(bx + 6, cy + 2, cw - 4, ch - 4); break;
        case PANEL_MODEL: model_panel(bx + 6, cy + 2, cw - 4, ch - 4); break;
        case PANEL_TRAIN: train_panel(bx + 6, cy + 2, cw - 4, ch - 4); break;
        case PANEL_INFER: infer_panel(bx + 6, cy + 2, cw - 4, ch - 4); break;
        case PANEL_EXPORT: export_panel(bx + 6, cy + 2, cw - 4, ch - 4); break;
    }

    vga_drawstring(bx + 4, by + h - 10, "Tab:switch  F1-F5:panels  Esc:close", 8, 1);
}

void aistudio_keypress(int id, char c) {
    (void)id;
    if (c == 0x09) { /* Tab */
        panel = (panel + 1) % 5;
        return;
    }
    if (c >= 0x84 && c <= 0x88) { /* Ctrl+1..5 = F1..F5 */
        int p = c - 0x84;
        if (p >= 0 && p < 5) panel = p;
        return;
    }
    if (c == 0x87) { /* Ctrl+Q = close */
        wm_close(id);
        return;
    }

    switch (panel) {
        case PANEL_DS:
            if (c == 'n' || c == 'N') {
                ds_ni = 2; ds_no = 1; ds_count = 0;
                ds_create_mgr(ds_ni, ds_no);
                vga_drawstring(10, 10, "Dataset ready (2->1)", 10, 1);
            }
            if (c == 'l' || c == 'L') {
                char fname[32]; int fi;
                for (fi = 0; fi < 31 && ds_path[fi]; fi++) fname[fi] = ds_path[fi];
                fname[fi] = 0;
                if (fi == 0) { const char *def = "DS.BIN"; for (fi = 0; def[fi]; fi++) fname[fi] = def[fi]; fname[fi] = 0; }
                if (ai_import_text_ds(fname, ds_ni, ds_no) == 0) {
                    ds_count = ds_mgr_count();
                    vga_drawstring(10, 10, "Dataset loaded", 10, 1);
                }
            }
            if (c == 's' || c == 'S') {
                char fname[32]; int fi;
                for (fi = 0; fi < 31 && ds_path[fi]; fi++) fname[fi] = ds_path[fi];
                fname[fi] = 0;
                if (fi == 0) { const char *def = "DS.BIN"; for (fi = 0; def[fi]; fi++) fname[fi] = def[fi]; fname[fi] = 0; }
                if (ai_export_text_ds(fname) == 0) vga_drawstring(10, 10, "Dataset saved", 10, 1);
            }
            if (c == 'c' || c == 'C') { ds_clear_mgr(); ds_count = 0; }
            if (c == 'v' || c == 'V') ai_open_ds_view();
            if (c == 'a' || c == 'A') {
                char sample[32]; int i;
                for (i = 0; i < ds_ni && i < 16; i++) sample[i] = '0' + (i % 2);
                sample[i] = ' '; i++;
                for (int j = 0; j < ds_no && i < 31; j++) sample[i++] = '0' + (j % 2);
                sample[i] = 0;
                ds_add_sample(sample);
                ds_count = ds_mgr_count();
            }
            break;

        case PANEL_MODEL:
            if (c == 'c' || c == 'C') {
                ai_create_net(model_layers, model_sizes);
                ai_set_act(0, model_act);
                vga_drawstring(10, 10, "Model created", 10, 1);
            }
            if (c == 'l' || c == 'L') {
                char fname[32]; int fi;
                for (fi = 0; fi < 31 && model_path[fi]; fi++) fname[fi] = model_path[fi];
                fname[fi] = 0;
                if (fi == 0) { const char *def = "MODEL.BIN"; for (fi = 0; def[fi]; fi++) fname[fi] = def[fi]; fname[fi] = 0; }
                if (ai_load_model(fname) == 0) vga_drawstring(10, 10, "Model loaded", 10, 1);
            }
            if (c == 's' || c == 'S') {
                char fname[32]; int fi;
                for (fi = 0; fi < 31 && model_path[fi]; fi++) fname[fi] = model_path[fi];
                fname[fi] = 0;
                if (fi == 0) { const char *def = "MODEL.BIN"; for (fi = 0; def[fi]; fi++) fname[fi] = def[fi]; fname[fi] = 0; }
                if (ai_save_model(fname) == 0) vga_drawstring(10, 10, "Model saved", 10, 1);
            }
            if (c == 'a' || c == 'A') { ai_set_auto(1); vga_drawstring(10, 10, "Auto-train ON", 10, 1); }
            if (c >= '1' && c <= '4') {
                ai_select_slot(c - '1');
                char ibuf[80]; ai_get_info(ibuf, 80);
            }
            break;

        case PANEL_TRAIN:
            if (c == 't' || c == 'T') {
                if (ds_count == 0) vga_drawstring(10, 10, "No samples!", 12, 1);
                train_running = 1;
                train_progress = 0;
                int r = ai_train_net(train_epochs, train_lr);
                train_running = 0;
                if (r == 0) vga_drawstring(10, 10, "Training complete", 10, 1);
                else if (r == -2) vga_drawstring(10, 10, "No dataset!", 12, 1);
                else vga_drawstring(10, 10, "Training error", 12, 1);
            }
            if (c == 's' || c == 'S') { train_running = 0; }
            if (c == 'r' || c == 'R') { train_progress = 0; }
            break;

        case PANEL_INFER:
            if (c == 8 && infer_pos > 0) infer_input[--infer_pos] = 0;
            else if (c == 13 && infer_input[0]) {
                char result[64];
                int rn = ai_infer_str(infer_input, result, 64);
                if (rn <= 0) vga_drawstring(10, 10, "Infer err", 12, 1);
            } else if (c >= 32 && c < 127 && infer_pos < 60) {
                infer_input[infer_pos++] = c;
                infer_input[infer_pos] = 0;
            }
            if (c == 'c' || c == 'C') { infer_input[0] = 0; infer_pos = 0; }
            break;

        case PANEL_EXPORT:
            if (c == 't' || c == 'T') {
                if (ai_export_txt("NNOUT.TXT") == 0)
                    vga_drawstring(10, 10, "Exported to NNOUT.TXT", 10, 1);
            }
            if (c == 'c' || c == 'C') {
                if (ai_export_txt("AINET.TXT") == 0)
                    vga_drawstring(10, 10, "Exported to AINET.TXT", 10, 1);
            }
            if (c == 's' || c == 'S') {
                for (int s = 0; s < ai_get_slot_count(); s++) {
                    char fn[32]; fn[0] = 'M'; fn[1] = 'O'; fn[2] = 'D';
                    itoa(s, fn + 3); fn[3 + strlen(fn + 3)] = 0;
                    int flen = strlen(fn);
                    if (flen < 28) { fn[flen] = '.'; fn[flen+1] = 'B'; fn[flen+2] = 'N'; fn[flen+3] = 0; }
                    ai_save_model_slot(s, fn);
                }
                vga_drawstring(10, 10, "All slots saved", 10, 1);
            }
            if (c == 'l' || c == 'L') {
                for (int s = 0; s < ai_get_slot_count(); s++) {
                    char fn[32]; fn[0] = 'M'; fn[1] = 'O'; fn[2] = 'D';
                    itoa(s, fn + 3);
                    int flen = strlen(fn);
                    if (flen < 28) { fn[flen] = '.'; fn[flen+1] = 'B'; fn[flen+2] = 'N'; fn[flen+3] = 0; }
                    ai_load_model_slot(s, fn);
                }
                vga_drawstring(10, 10, "All slots loaded", 10, 1);
            }
            break;
    }
}

void aistudio_click(int id, int mx, int my) {
    window_t *win = wm_get(id);
    if (!win) return;
    int bx = win->x, by = win->y;
    int tw = (win->w - 20) / 5;
    if (tw < 30) tw = 30;
    for (int i = 0; i < 5; i++) {
        int px = bx + 4 + i * (tw + 2);
        if (my >= by + 14 && my < by + 24 && mx >= px && mx < px + tw) {
            panel = i;
            return;
        }
    }
}
