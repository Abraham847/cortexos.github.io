#include "kernel.h"
#include "lang.h"
#include "nn_float.h"

#define PRESET_N 4
#define TOTAL_FIELDS 21

static const char *preset_names[PRESET_N] = {"XOR 2-4-1", "AND 2-2-1", "OR 2-2-1", "Custom"};
static int preset_sel;

static int layers[NN_MAX_L];
static int layer_count;
static int act_sel;
static int use_float;

static int lr_val;
static int ep_val;
static int mu_val;
static int decay_val;
static int val_pct;
static int ds_type;
static int opt_sel;      /* 0=SGD 1=MOM 2=ADAM */
static int wd_val;       /* weight decay (L2) *10000 */
static int clip_val;     /* clip norm *100 */
static int lr_step_val;  /* LR scheduler step */
static int lr_fact_val;  /* LR scheduler factor *100 */
static int es_val;       /* early stop patience */
static int adv_init;     /* 1 = Xavier init */

static int field;
static int edit_pos;

static const char *ds_names[5] = {"XOR", "AND", "OR", "CIRCLE", "SPIRAL"};
static const char *opt_names[3] = {"SGD", "MOM", "ADAM"};

static void apply_preset(int pi) {
    switch (pi) {
        case 0: layer_count = 3; layers[0] = 2; layers[1] = 4; layers[2] = 1; act_sel = 0; break;
        case 1: layer_count = 3; layers[0] = 2; layers[1] = 2; layers[2] = 1; act_sel = 0; break;
        case 2: layer_count = 3; layers[0] = 2; layers[1] = 2; layers[2] = 1; act_sel = 0; break;
        case 3: layer_count = 3; layers[0] = 2; layers[1] = 4; layers[2] = 1; act_sel = 1; break;
    }
}

static void do_create(void) {
    if (use_float) {
        nn_float_t nf;
        if (nnf_init(&nf, layer_count, layers)) return;
        nnf_rand(&nf, 2.0f, 123);
        nnf_save(&nf, "AIMODEL.BIN");
        nnf_free(&nf);
    } else {
        char ds_cmd[8] = {ds_names[ds_type][0], 0};
        ds_generate(ds_cmd);
        ds_save_mgr("TRAINDATA.BIN");

        ai_create_net(layer_count, layers);
        int active = ai_get_slot_count() - 1;
        if (active < 0) active = 0;
        ai_select_slot(active);
        for (int i = 1; i < layer_count; i++)
            ai_set_act(i, act_sel);
        ai_load_dataset("TRAINDATA.BIN");
        ai_set_lr(lr_val);
        ai_set_epochs(ep_val);
        ai_set_mu(opt_sel == 1 ? mu_val : 0);
        ai_set_lr_decay(decay_val);
        ai_set_val_pct(val_pct);
        ai_set_opt(opt_sel);
        ai_set_weight_decay(wd_val);
        ai_set_clip(clip_val);
        ai_set_lr_step(lr_step_val);
        ai_set_lr_factor(lr_fact_val);
        ai_set_es_patience(es_val);
        ai_set_adam_beta1(90);
        ai_set_adam_beta2(999);
        ai_save_model("AIMODEL.BIN");
        ai_open_trainer(active);
    }
}

void aicreator_init(void) {
    preset_sel = 0;
    layer_count = 3;
    layers[0] = 2; layers[1] = 4; layers[2] = 1;
    act_sel = 0;
    lr_val = 50;
    ep_val = 100;
    mu_val = 0;
    decay_val = 100;
    val_pct = 0;
    ds_type = 0;
    opt_sel = 0;
    wd_val = 0;
    clip_val = 0;
    lr_step_val = 0;
    lr_fact_val = 100;
    es_val = 0;
    adv_init = 0;
    use_float = 0;
    field = 0;
    edit_pos = 0;
}

static void draw_net_preview(int bx, int by, int w) {
    if (layer_count < 2) return;
    int max_n = 1;
    for (int i = 0; i < layer_count; i++)
        if (layers[i] > max_n) max_n = layers[i];
    int rw = 14, rh = 5;
    int gap = (w - layer_count * rw) / (layer_count + 1);
    if (gap < 3) gap = 3;
    int cy_base = by + 25;
    for (int li = 0; li < layer_count; li++) {
        int cx = bx + gap + li * (rw + gap);
        int n = layers[li];
        int start_y = cy_base - (n * (rh + 2)) / 2;
        for (int ni = 0; ni < n; ni++) {
            int cy = start_y + ni * (rh + 2);
            vga_fillrect(cx, cy, rw, rh, 6);
            vga_drawrect(cx, cy, rw, rh, 7);
            if (li < layer_count - 1) {
                int n2 = layers[li + 1];
                int sy1 = cy_base - (n2 * (rh + 2)) / 2 + rh / 2;
                int y1 = cy + rh / 2;
                for (int ni2 = 0; ni2 < n2 && ni2 < 4; ni2++) {
                    int y2 = sy1 + ni2 * (rh + 2);
                    vga_putpixel(cx + rw, y1, 8);
                    vga_putpixel(cx + rw + 1, y1 + (y2 - y1) / 4, 8);
                }
            }
        }
    }
    char buf[16];
    vga_drawstring(bx, by + 52, "Arch:", 7, 1);
    int px = bx + 40;
    for (int i = 0; i < layer_count; i++) {
        itoa(layers[i], buf);
        vga_drawstring(px, by + 52, buf, 11, 1);
        px += strlen(buf) * 9 + 4;
        if (i < layer_count - 1) vga_drawstring(px - 4, by + 52, "-", 8, 1);
    }
}

static void draw_hbar(int x, int y, int w, int h, int val, int maxv, u8 col) {
    int bw = w * val / maxv;
    if (bw > 0) vga_fillrect(x, y, bw, h, col);
    vga_drawrect(x, y, w, h, 8);
}

void aicreator_draw(int id) {
    window_t *win = wm_get(id);
    if (!win) return;
    int bx = win->x, by = win->y;
    int w = win->w, h = win->h;
    vga_fillrect(bx, by, w, h, 1);
    vga_drawstring(bx + 4, by + 4, "AI Creator", 14, 1);
    vga_drawrect(bx + 4, by + 14, w - 8, 1, 8);
    int y = by + 20;
    int x = bx + 4;
    int col2 = bx + 90;
    char buf[16];

    /* 0: Preset */
    vga_drawstring(x, y, "Preset:", 7, 1);
    for (int i = 0; i < PRESET_N; i++) {
        int px = col2 + i * 42;
        u8 fg = (field == 0 && preset_sel == i) ? 15 : 7;
        u8 bg = (field == 0 && preset_sel == i) ? 4 : 1;
        vga_drawrect(px, y, 40, 10, fg);
        vga_fillrect(px + 1, y + 1, 38, 8, bg);
        vga_drawstring(px + 2, y + 1, preset_names[i], fg, bg);
    }
    y += 12;

    /* 1: Layers */
    vga_drawstring(x, y, "Layers:", 7, 1);
    itoa(layer_count, buf);
    u8 lfg = field == 1 ? 15 : 7;
    u8 lbg = field == 1 ? 4 : 1;
    vga_drawrect(col2, y, 20, 10, lfg);
    vga_fillrect(col2 + 1, y + 1, 18, 8, lbg);
    vga_drawstring(col2 + 4, y + 1, buf, lfg, lbg);
    y += 12;

    /* 2: Neurons */
    vga_drawstring(x, y, "Neurons:", 7, 1);
    for (int i = 0; i < layer_count && i < 4; i++) {
        int px = col2 + i * 26;
        itoa(layers[i], buf);
        u8 nfg = (field == 2 && edit_pos == i) ? 15 : 7;
        u8 nbg = (field == 2 && edit_pos == i) ? 4 : 1;
        vga_drawrect(px, y, 22, 10, nfg);
        vga_fillrect(px + 1, y + 1, 20, 8, nbg);
        vga_drawstring(px + 2, y + 1, buf, nfg, nbg);
    }
    y += 12;

    /* 3: Act (4 options including Leaky ReLU) */
    vga_drawstring(x, y, "Act:", 7, 1);
    static const char *act_names[4] = {"Sigmoid", "ReLU", "Tanh", "LReLU"};
    for (int i = 0; i < 4; i++) {
        int px = col2 + i * 40;
        u8 afg = (field == 3 && act_sel == i) ? 15 : 7;
        u8 abg = (field == 3 && act_sel == i) ? 4 : 1;
        vga_drawrect(px, y, 36, 10, afg);
        vga_fillrect(px + 1, y + 1, 34, 8, abg);
        vga_drawstring(px + 2, y + 1, act_names[i], afg, abg);
    }
    y += 12;

    /* 4: Optimizer (SGD/MOM/ADAM) */
    vga_drawstring(x, y, "Optim:", 7, 1);
    for (int i = 0; i < 3; i++) {
        int px = col2 + i * 34;
        u8 ofg = (field == 4 && opt_sel == i) ? 15 : 7;
        u8 obg = (field == 4 && opt_sel == i) ? 4 : 1;
        vga_drawrect(px, y, 30, 10, ofg);
        vga_fillrect(px + 1, y + 1, 28, 8, obg);
        vga_drawstring(px + 2, y + 1, opt_names[i], ofg, obg);
    }
    y += 12;

    /* 5: LR */
    vga_drawstring(x, y, "LR:", 7, 1);
    itoa(lr_val, buf);
    u8 lffg = field == 5 ? 15 : 7;
    u8 lfbg = field == 5 ? 4 : 1;
    vga_drawrect(col2, y, 36, 10, lffg);
    vga_fillrect(col2 + 1, y + 1, 34, 8, lfbg);
    vga_drawstring(col2 + 2, y + 1, buf, lffg, lfbg);
    vga_drawstring(col2 + 30, y + 1, "/100", 8, 1);
    y += 12;

    /* 6: Epochs */
    vga_drawstring(x, y, "Epochs:", 7, 1);
    itoa(ep_val, buf);
    u8 efg = field == 6 ? 15 : 7;
    u8 ebg = field == 6 ? 4 : 1;
    vga_drawrect(col2, y, 36, 10, efg);
    vga_fillrect(col2 + 1, y + 1, 34, 8, ebg);
    vga_drawstring(col2 + 2, y + 1, buf, efg, ebg);
    y += 12;

    /* 7: Momentum (only for MOM) */
    vga_drawstring(x, y, "Mu:", 7, 1);
    itoa(mu_val, buf);
    u8 mufg = (field == 7 && opt_sel == 1) ? 15 : 7;
    u8 mubg = (field == 7 && opt_sel == 1) ? 4 : 1;
    vga_drawrect(col2, y, 36, 10, mufg);
    vga_fillrect(col2 + 1, y + 1, 34, 8, mubg);
    vga_drawstring(col2 + 2, y + 1, buf, mufg, mubg);
    vga_drawstring(col2 + 30, y + 1, "/95", 8, 1);
    y += 12;

    /* 8: Decay */
    vga_drawstring(x, y, "Decay:", 7, 1);
    itoa(decay_val, buf);
    u8 defg = field == 8 ? 15 : 7;
    u8 debg = field == 8 ? 4 : 1;
    vga_drawrect(col2, y, 36, 10, defg);
    vga_fillrect(col2 + 1, y + 1, 34, 8, debg);
    vga_drawstring(col2 + 2, y + 1, buf, defg, debg);
    vga_drawstring(col2 + 30, y + 1, "/100", 8, 1);
    y += 12;

    /* 9: Val % */
    vga_drawstring(x, y, "Val %:", 7, 1);
    itoa(val_pct, buf);
    u8 vafg = field == 9 ? 15 : 7;
    u8 vabg = field == 9 ? 4 : 1;
    vga_drawrect(col2, y, 36, 10, vafg);
    vga_fillrect(col2 + 1, y + 1, 34, 8, vabg);
    vga_drawstring(col2 + 2, y + 1, buf, vafg, vabg);
    vga_drawstring(col2 + 30, y + 1, "%", 8, 1);
    y += 12;

    /* 10: Dataset */
    vga_drawstring(x, y, "Dataset:", 7, 1);
    for (int d = 0; d < 5; d++) {
        int px = col2 + d * 32;
        u8 dfg = (field == 10 && ds_type == d) ? 15 : 7;
        u8 dbg = (field == 10 && ds_type == d) ? 4 : 1;
        vga_drawrect(px, y, 28, 10, dfg);
        vga_fillrect(px + 1, y + 1, 26, 8, dbg);
        vga_drawstring(px + 2, y + 1, ds_names[d], dfg, dbg);
    }
    y += 12;

    /* 11: Weight Decay (L2) */
    vga_drawstring(x, y, "L2 Wd:", 7, 1);
    itoa(wd_val, buf);
    u8 wdfg = field == 11 ? 15 : 7;
    u8 wdbg = field == 11 ? 4 : 1;
    vga_drawrect(col2, y, 36, 10, wdfg);
    vga_fillrect(col2 + 1, y + 1, 34, 8, wdbg);
    vga_drawstring(col2 + 2, y + 1, buf, wdfg, wdbg);
    vga_drawstring(col2 + 30, y + 1, "/1e4", 8, 1);
    y += 12;

    /* 12: Clip Norm */
    vga_drawstring(x, y, "Clip:", 7, 1);
    itoa(clip_val, buf);
    u8 clfg = field == 12 ? 15 : 7;
    u8 clbg = field == 12 ? 4 : 1;
    vga_drawrect(col2, y, 36, 10, clfg);
    vga_fillrect(col2 + 1, y + 1, 34, 8, clbg);
    vga_drawstring(col2 + 2, y + 1, buf, clfg, clbg);
    vga_drawstring(col2 + 30, y + 1, "/100", 8, 1);
    y += 12;

    /* 13: LR Step */
    vga_drawstring(x, y, "LRstep:", 7, 1);
    itoa(lr_step_val, buf);
    u8 lsfg = field == 13 ? 15 : 7;
    u8 lsbg = field == 13 ? 4 : 1;
    vga_drawrect(col2, y, 36, 10, lsfg);
    vga_fillrect(col2 + 1, y + 1, 34, 8, lsbg);
    vga_drawstring(col2 + 2, y + 1, buf, lsfg, lsbg);
    vga_drawstring(col2 + 30, y + 1, "ep", 8, 1);
    y += 12;

    /* 14: LR Factor */
    vga_drawstring(x, y, "LRfact:", 7, 1);
    itoa(lr_fact_val, buf);
    u8 lffg2 = field == 14 ? 15 : 7;
    u8 lfbg2 = field == 14 ? 4 : 1;
    vga_drawrect(col2, y, 36, 10, lffg2);
    vga_fillrect(col2 + 1, y + 1, 34, 8, lfbg2);
    vga_drawstring(col2 + 2, y + 1, buf, lffg2, lfbg2);
    vga_drawstring(col2 + 30, y + 1, "/100", 8, 1);
    y += 12;

    /* 15: Early Stop Patience */
    vga_drawstring(x, y, "ES top:", 7, 1);
    itoa(es_val, buf);
    u8 esfg = field == 15 ? 15 : 7;
    u8 esbg = field == 15 ? 4 : 1;
    vga_drawrect(col2, y, 36, 10, esfg);
    vga_fillrect(col2 + 1, y + 1, 34, 8, esbg);
    vga_drawstring(col2 + 2, y + 1, buf, esfg, esbg);
    vga_drawstring(col2 + 30, y + 1, "ep", 8, 1);
    y += 14;

    /* 16: Type */
    vga_drawstring(x, y, "Type:", 7, 1);
    static const char *tnames[2] = {"Fixed", "Float"};
    for (int i = 0; i < 2; i++) {
        int px = col2 + i * 38;
        u8 tfg = (field == 16 && use_float == i) ? 15 : 7;
        u8 tbg = (field == 16 && use_float == i) ? 4 : 1;
        vga_drawrect(px, y, 34, 10, tfg);
        vga_fillrect(px + 1, y + 1, 32, 8, tbg);
        vga_drawstring(px + 3, y + 1, tnames[i], tfg, tbg);
    }
    y += 12;

    /* Advanced init toggle (field 17) */
    vga_drawstring(x, y, "AdvInit:", 7, 1);
    static const char *init_names[2] = {"Rand", "Xavier"};
    for (int i = 0; i < 2; i++) {
        int px = col2 + i * 42;
        u8 ifg = (field == 17 && adv_init == i) ? 15 : 7;
        u8 ibg = (field == 17 && adv_init == i) ? 4 : 1;
        vga_drawrect(px, y, 38, 10, ifg);
        vga_fillrect(px + 1, y + 1, 36, 8, ibg);
        vga_drawstring(px + 2, y + 1, init_names[i], ifg, ibg);
    }
    y += 14;

    /* 18: CREATE button */
    int bpx = bx + w / 2 - 30;
    u8 cfg = field == 18 ? 15 : 14;
    u8 cbg = field == 18 ? 2 : 1;
    vga_drawrect(bpx, y, 60, 14, cfg);
    vga_fillrect(bpx + 1, y + 1, 58, 12, cbg);
    vga_drawstring(bpx + 6, y + 3, "CREATE", 15, cbg);
    y += 18;

    draw_net_preview(bx + 4, y, w - 8);
}

void aicreator_keypress(int id, char c) {
    if (c == 0x81) { field = (field + 1) % TOTAL_FIELDS; return; }
    if (c == 0x80) { field = (field - 1 + TOTAL_FIELDS) % TOTAL_FIELDS; return; }
    if (c == 0x83) {
        switch (field) {
            case 0: preset_sel = (preset_sel + 1) % PRESET_N; apply_preset(preset_sel); break;
            case 1: if (layer_count < NN_MAX_L) layer_count++; break;
            case 2: if (edit_pos < layer_count - 1) edit_pos++; break;
            case 3: act_sel = (act_sel + 1) % 4; break;
            case 4: opt_sel = (opt_sel + 1) % 3; break;
            case 5: if (lr_val < 99) lr_val++; break;
            case 6: { ep_val += 10; if (ep_val > 999) ep_val = 999; } break;
            case 7: if (mu_val < 95) mu_val += 5; break;
            case 8: if (decay_val < 100) decay_val += 5; break;
            case 9: if (val_pct < 50) val_pct += 5; break;
            case 10: ds_type = (ds_type + 1) % 5; break;
            case 11: if (wd_val < 100) wd_val += 5; break;
            case 12: if (clip_val < 100) clip_val += 5; break;
            case 13: lr_step_val += 10; if (lr_step_val > 200) lr_step_val = 0; break;
            case 14: if (lr_fact_val < 100) lr_fact_val += 5; break;
            case 15: es_val += 10; if (es_val > 200) es_val = 0; break;
            case 16: use_float = !use_float; break;
            case 17: adv_init = !adv_init; break;
        }
        return;
    }
    if (c == 0x82) {
        switch (field) {
            case 0: preset_sel = (preset_sel - 1 + PRESET_N) % PRESET_N; apply_preset(preset_sel); break;
            case 1: if (layer_count > 2) layer_count--; break;
            case 2: if (edit_pos > 0) edit_pos--; break;
            case 3: act_sel = (act_sel - 1 + 4) % 4; break;
            case 4: opt_sel = (opt_sel - 1 + 3) % 3; break;
            case 5: if (lr_val > 1) lr_val--; break;
            case 6: { ep_val -= 10; if (ep_val < 10) ep_val = 10; } break;
            case 7: if (mu_val >= 5) mu_val -= 5; break;
            case 8: if (decay_val >= 5) decay_val -= 5; break;
            case 9: if (val_pct >= 5) val_pct -= 5; break;
            case 10: ds_type = (ds_type - 1 + 5) % 5; break;
            case 11: if (wd_val >= 5) wd_val -= 5; break;
            case 12: if (clip_val >= 5) clip_val -= 5; break;
            case 13: { lr_step_val -= 10; if (lr_step_val < 0) lr_step_val = 0; } break;
            case 14: if (lr_fact_val >= 5) lr_fact_val -= 5; break;
            case 15: { es_val -= 10; if (es_val < 0) es_val = 0; } break;
            case 16: use_float = !use_float; break;
            case 17: adv_init = !adv_init; break;
        }
        return;
    }
    if (c == '+' || c == '=') {
        switch (field) {
            case 1: if (layer_count < NN_MAX_L) layer_count++; break;
            case 2: if (layers[edit_pos] < 32) layers[edit_pos]++; break;
            case 5: if (lr_val < 99) lr_val++; break;
            case 6: { ep_val += 10; if (ep_val > 999) ep_val = 999; } break;
            case 7: if (mu_val <= 95) mu_val += 5; break;
            case 8: if (decay_val <= 95) decay_val += 5; break;
            case 9: if (val_pct <= 45) val_pct += 5; break;
            case 11: if (wd_val <= 95) wd_val += 5; break;
            case 12: if (clip_val <= 95) clip_val += 5; break;
            case 14: if (lr_fact_val <= 95) lr_fact_val += 5; break;
        }
        return;
    }
    if (c == '-') {
        switch (field) {
            case 1: if (layer_count > 2) layer_count--; break;
            case 2: if (layers[edit_pos] > 1) layers[edit_pos]--; break;
            case 5: if (lr_val > 1) lr_val--; break;
            case 6: { ep_val -= 10; if (ep_val < 10) ep_val = 10; } break;
            case 7: if (mu_val >= 5) mu_val -= 5; break;
            case 8: if (decay_val >= 5) decay_val -= 5; break;
            case 9: if (val_pct >= 5) val_pct -= 5; break;
            case 11: if (wd_val >= 5) wd_val -= 5; break;
            case 12: if (clip_val >= 5) clip_val -= 5; break;
            case 14: if (lr_fact_val >= 5) lr_fact_val -= 5; break;
        }
        return;
    }
    if (c == 13 || c == ' ') {
        if (field == 18) do_create();
        return;
    }
}

void aicreator_click(int id, int mx, int my) {
    window_t *win = wm_get(id);
    if (!win) return;
    int bx = win->x, by = win->y;
    int col2 = bx + 90;
    int y = by + 20;

    /* 0: Preset */
    if (my >= y && my < y + 10) {
        for (int i = 0; i < PRESET_N; i++) {
            int px = col2 + i * 42;
            if (mx >= px && mx < px + 40) { preset_sel = i; apply_preset(i); return; }
        }
    }
    y += 12;
    /* 1: Layers */
    if (my >= y && my < y + 10 && mx >= col2 && mx < col2 + 20) { field = 1; return; }
    y += 12;
    /* 2: Neurons */
    if (my >= y && my < y + 10 && mx >= col2) {
        field = 2;
        for (int i = 0; i < layer_count && i < 4; i++) {
            int px = col2 + i * 26;
            if (mx >= px && mx < px + 22) { edit_pos = i; return; }
        }
        return;
    }
    y += 12;
    /* 3: Act */
    if (my >= y && my < y + 10) {
        for (int i = 0; i < 4; i++) {
            int px = col2 + i * 40;
            if (mx >= px && mx < px + 36) { act_sel = i; field = 3; return; }
        }
    }
    y += 12;
    /* 4: Optimizer */
    if (my >= y && my < y + 10) {
        for (int i = 0; i < 3; i++) {
            int px = col2 + i * 34;
            if (mx >= px && mx < px + 30) { opt_sel = i; field = 4; return; }
        }
    }
    y += 12;
    /* 5: LR */
    if (my >= y && my < y + 10 && mx >= col2 && mx < col2 + 36) { field = 5; return; }
    y += 12;
    /* 6: Epochs */
    if (my >= y && my < y + 10 && mx >= col2 && mx < col2 + 36) { field = 6; return; }
    y += 12;
    /* 7: Mu */
    if (my >= y && my < y + 10 && mx >= col2 && mx < col2 + 36) { field = 7; return; }
    y += 12;
    /* 8: Decay */
    if (my >= y && my < y + 10 && mx >= col2 && mx < col2 + 36) { field = 8; return; }
    y += 12;
    /* 9: Val % */
    if (my >= y && my < y + 10 && mx >= col2 && mx < col2 + 36) { field = 9; return; }
    y += 12;
    /* 10: Dataset */
    if (my >= y && my < y + 10) {
        for (int d = 0; d < 5; d++) {
            int px = col2 + d * 32;
            if (mx >= px && mx < px + 28) { ds_type = d; field = 10; return; }
        }
    }
    y += 12;
    /* 11: Weight Decay */
    if (my >= y && my < y + 10 && mx >= col2 && mx < col2 + 36) { field = 11; return; }
    y += 12;
    /* 12: Clip */
    if (my >= y && my < y + 10 && mx >= col2 && mx < col2 + 36) { field = 12; return; }
    y += 12;
    /* 13: LR Step */
    if (my >= y && my < y + 10 && mx >= col2 && mx < col2 + 36) { field = 13; return; }
    y += 12;
    /* 14: LR Factor */
    if (my >= y && my < y + 10 && mx >= col2 && mx < col2 + 36) { field = 14; return; }
    y += 12;
    /* 15: Early Stop */
    if (my >= y && my < y + 10 && mx >= col2 && mx < col2 + 36) { field = 15; return; }
    y += 14;
    /* 16: Type */
    if (my >= y && my < y + 10) {
        for (int i = 0; i < 2; i++) {
            int px = col2 + i * 38;
            if (mx >= px && mx < px + 34) { use_float = i; field = 16; return; }
        }
    }
    y += 12;
    /* 17: Adv Init */
    if (my >= y && my < y + 10) {
        for (int i = 0; i < 2; i++) {
            int px = col2 + i * 42;
            if (mx >= px && mx < px + 38) { adv_init = i; field = 17; return; }
        }
    }
    y += 14;
    /* 18: CREATE */
    int bpx = bx + win->w / 2 - 30;
    if (my >= y && my < y + 14 && mx >= bpx && mx < bpx + 60) { field = 18; do_create(); }
}
