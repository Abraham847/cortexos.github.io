#include "nn.h"
#include "heap.h"
#include "ata.h"
#include "fs.h"
#include "task.h"
#include "lang.h"

/* ---- Multi-network ---- */
#define NN_SLOTS 4
static struct {
    nn net; int ready;
    fp loss; int steps;
    int ds_idx;
    int auto_mode;
    int saved;
    int win_id;
    int edit_win;
    int wgt_win;
    fp lr;
    int epochs;
} slots[NN_SLOTS];
static int slot_count;

static int active_slot;

/* ---- Shared dataset ---- */
static dataset ds;
static int ds_loaded;

/* ---- Loss history (per-slot) ---- */
#define LOSS_HIST 50
static int loss_hist[NN_SLOTS][LOSS_HIST];
static int loss_pos[NN_SLOTS];

/* ---- Global defaults ---- */
static fp default_lr = FF(0.5);
static int default_epochs = 100;

/* ---- Window type tags (stored in win->title as "[tag]...") ---- */
#define TAG_TRAIN  'T'
#define TAG_EDIT   'E'
#define TAG_DS     'D'

/* ===== HELPERS ===== */
static void record_loss(int si) {
    int v = FI(slots[si].loss * 100 / F1);
    if (v > 100) v = 100; if (v < 0) v = 0;
    loss_hist[si][loss_pos[si]] = v;
    loss_pos[si] = (loss_pos[si] + 1) % LOSS_HIST;
}

static int alloc_slot(void) {
    for (int i = 0; i < NN_SLOTS; i++)
        if (!slots[i].ready) return i;
    return -1;
}

static void slot_init_net(int si, int nl, int *sz) {
    if (slots[si].ready) nn_free(&slots[si].net);
    nn_init(&slots[si].net, nl, sz);
    nn_rand(&slots[si].net, FF(2));
    slots[si].ready = 1;
    slots[si].loss = F1;
    slots[si].steps = 0;
    slots[si].auto_mode = 0;
    slots[si].saved = 0;
    slots[si].ds_idx = -1;
    slots[si].win_id = -1;
    slots[si].edit_win = -1;
    slots[si].wgt_win = -1;
    slots[si].lr = default_lr;
    slots[si].epochs = default_epochs;
    for (int i = 0; i < LOSS_HIST; i++) loss_hist[si][i] = 100;
    loss_pos[si] = 0;
    if (si >= slot_count) slot_count = si + 1;
    active_slot = si;
}

/* ===== INFO ===== */
static void scpy(char *d, int *pos, const char *s, int max) {
    while (*s && *pos < max - 1) d[(*pos)++] = *s++;
}
void ai_get_info(char *buf, int max) {
    int si = active_slot;
    int pos = 0;
    if (si < 0 || si >= slot_count || !slots[si].ready) { buf[0] = 0; return; }
    nn *n = &slots[si].net;
    char tmp[16];
    scpy(buf, &pos, "Lyr:", max);
    itoa(n->nl, tmp);
    scpy(buf, &pos, tmp, max);
    scpy(buf, &pos, " Sz:", max);
    for (int li = 0; li < n->nl; li++) {
        itoa(n->sz[li], tmp);
        scpy(buf, &pos, tmp, max);
        scpy(buf, &pos, " ", max);
    }
    scpy(buf, &pos, "Act:", max);
    for (int li = 0; li < n->nl; li++) {
        scpy(buf, &pos, nn_act_name(n->acts[li]), max);
        scpy(buf, &pos, " ", max);
    }
    if (pos < max) buf[pos] = 0;
}

/* ===== INIT ===== */
void ai_init(void) {
    for (int i = 0; i < NN_SLOTS; i++) slots[i].ready = 0;
    slot_count = 0;
    ds_loaded = 0;
    default_lr = FF(0.5);
    default_epochs = 100;
    slot_init_net(0, 3, (int[]){2, 6, 1});
}

/* ===== API ===== */
void ai_create_net(int nl, int *sz) {
    int si = alloc_slot();
    if (si < 0) return;
    slot_init_net(si, nl, sz);
}

int ai_train_net(int ep, int lr_int) {
    int si = active_slot;
    if (!slots[si].ready) return -1;
    slots[si].lr = (fp)((long long)lr_int * F1 / 100);
    slots[si].epochs = ep > 0 ? ep : 1;
    slots[si].auto_mode = 0;
    int di = slots[si].ds_idx;
    if (di >= 0 && ds_loaded) {
        for (int e = 0; e < ep; e++)
            slots[si].loss = ds_train_epoch(&slots[si].net, &ds, slots[si].lr);
        slots[si].steps += ep * ds.n;
        nn_fwd(&slots[si].net, ds.in);
        record_loss(si);
        return 0;
    }
    return -2;
}

int ai_infer_slot(int si, fp *in, fp *out) {
    if (si < 0 || si >= slot_count || !slots[si].ready) return -1;
    nn *n = &slots[si].net;
    return nn_infer(n, in, out);
}

int ai_save_model_slot(int si, const char *path) {
    if (si < 0 || si >= slot_count || !slots[si].ready) return -1;
    if (nn_save_file(&slots[si].net, path) == 0) { slots[si].saved = 1; return 0; }
    return -1;
}

int ai_load_model_slot(int si, const char *path) {
    if (si < 0 || si >= slot_count) return -1;
    nn saved;
    if (nn_load_file(&saved, path) != 0) return -1;
    if (slots[si].ready) nn_free(&slots[si].net);
    slots[si].net = saved;
    slots[si].ready = 1;
    slots[si].saved = 0;
    slots[si].steps = 0;
    slots[si].loss = F1;
    slots[si].auto_mode = 0;
    slots[si].ds_idx = -1;
    if (si >= slot_count) slot_count = si + 1;
    active_slot = si;
    return 0;
}

void ai_set_act(int layer, int act) {
    int si = active_slot;
    if (!slots[si].ready || layer < 0 || layer >= slots[si].net.nl) return;
    slots[si].net.acts[layer] = (nn_act_t)act;
}

void ai_set_auto(int on) {
    int si = active_slot;
    slots[si].auto_mode = on ? 1 : 0;
}

void ai_set_lr(int lr_int) {
    default_lr = (fp)((long long)lr_int * F1 / 100);
    int si = active_slot;
    if (si >= 0 && si < slot_count && slots[si].ready) slots[si].lr = default_lr;
}
void ai_set_epochs(int ep) {
    default_epochs = ep > 0 ? ep : 100;
    int si = active_slot;
    if (si >= 0 && si < slot_count && slots[si].ready) slots[si].epochs = default_epochs;
}

/* ===== OLD API WRAPPERS (delegate to active slot) ===== */
int ai_save_model(const char *path) {
    int si = active_slot;
    if (si < 0 || si >= slot_count || !slots[si].ready) return -1;
    return ai_save_model_slot(si, path);
}
int ai_load_model(const char *path) {
    int si = alloc_slot();
    if (si < 0) return -1;
    return ai_load_model_slot(si, path);
}
int ai_export_txt(const char *path) {
    int si = active_slot;
    if (!slots[si].ready) return -1;
    char *buf = (char*)kmalloc(32768);
    if (!buf) return -1;
    int n = nn_export_txt(&slots[si].net, buf, 32768);
    if (n < 0) { kfree(buf); return -1; }
    int r = fs_write(path, (u8*)buf, n);
    kfree(buf);
    return r == n ? 0 : -1;
}
int ai_infer_str(const char *in_str, char *out_str, int max) {
    int si = active_slot;
    if (!slots[si].ready) return -1;
    nn *n = &slots[si].net;
    fp in[NN_MAX_L];
    int count = 0;
    while (*in_str && count < n->sz[0]) {
        while (*in_str == ' ') in_str++;
        if ((*in_str < '0' || *in_str > '9') && *in_str != '-') { in_str++; continue; }
        int val = 0, neg = 0;
        if (*in_str == '-') { neg = 1; in_str++; }
        while (*in_str >= '0' && *in_str <= '9') val = val * 10 + (*in_str++ - '0');
        in[count++] = FF(neg ? -val : val);
    }
    if (count != n->sz[0]) return -2;
    nn_fwd(n, in);
    int pos = 0;
    int ol = n->sz[n->nl - 1];
    for (int i = 0; i < ol && pos < max - 10; i++) {
        if (i > 0) out_str[pos++] = ' ';
        int v = FI(n->a[n->nl - 1].d[i] * 100);
        char dbuf[16]; itoa(v, dbuf);
        for (int j = 0; dbuf[j] && pos < max - 1; j++) out_str[pos++] = dbuf[j];
    }
    if (pos < max) out_str[pos] = 0;
    return pos;
}

/* ===== BACKGROUND TASK ===== */
void ai_bg_task(void) {
    while (1) {
        for (int si = 0; si < slot_count; si++) {
            if (!slots[si].ready || !slots[si].auto_mode) continue;
            int di = slots[si].ds_idx;
            if (di >= 0 && ds_loaded) {
                nn_fwd(&slots[si].net, ds.in + di * ds.ni);
                slots[si].loss = ds_train_epoch(&slots[si].net, &ds, slots[si].lr);
                slots[si].steps += ds.n;
                record_loss(si);
            }
        }
        task_yield();
    }
}

/* ===== DATASET LOAD ===== */
int ai_load_dataset(const char *path) {
    dataset tmp;
    if (ds_load(&tmp, path) == 0) {
        if (ds_loaded) { kfree(ds.in); kfree(ds.out); }
        ds = tmp; ds_loaded = 1;
        int si = alloc_slot();
        if (si >= 0) {
            slot_init_net(si, 3, (int[]){ds.ni, ds.ni > 4 ? ds.ni * 2 : 6, ds.no});
            slots[si].ds_idx = 0;
            nn_fwd(&slots[si].net, ds.in);
        }
        return 0;
    }
    return -1;
}

/* ===== DATASET MANAGER (shell DS commands) ===== */
static dataset mgr_ds;
static int mgr_ready;

int ds_create_mgr(int ni, int no) {
    if (mgr_ready) { kfree(mgr_ds.in); kfree(mgr_ds.out); }
    mgr_ds.n = 0; mgr_ds.ni = ni; mgr_ds.no = no;
    mgr_ds.in = 0; mgr_ds.out = 0; mgr_ready = 1; return 0;
}

int ds_add_sample(const char *str) {
    if (!mgr_ready || mgr_ds.ni + mgr_ds.no == 0) return -1;
    int vals[16], nv = 0;
    while (*str && nv < 16) {
        while (*str == ' ') str++;
        if ((*str < '0' || *str > '9') && *str != '-') break;
        int v = 0, neg = 0;
        if (*str == '-') { neg = 1; str++; }
        while (*str >= '0' && *str <= '9') v = v * 10 + (*str++ - '0');
        vals[nv++] = neg ? -v : v;
    }
    if (nv != mgr_ds.ni + mgr_ds.no) return -2;
    int nn = mgr_ds.n + 1;
    fp *ni2 = (fp*)kmalloc(nn * mgr_ds.ni * sizeof(fp));
    fp *no2 = (fp*)kmalloc(nn * mgr_ds.no * sizeof(fp));
    if (!ni2 || !no2) { kfree(ni2); kfree(no2); return -3; }
    for (int i = 0; i < mgr_ds.n; i++) {
        for (int j = 0; j < mgr_ds.ni; j++) ni2[i * mgr_ds.ni + j] = mgr_ds.in[i * mgr_ds.ni + j];
        for (int j = 0; j < mgr_ds.no; j++) no2[i * mgr_ds.no + j] = mgr_ds.out[i * mgr_ds.no + j];
    }
    for (int j = 0; j < mgr_ds.ni; j++) ni2[mgr_ds.n * mgr_ds.ni + j] = FF(vals[j]);
    for (int j = 0; j < mgr_ds.no; j++) no2[mgr_ds.n * mgr_ds.no + j] = FF(vals[mgr_ds.ni + j]);
    kfree(mgr_ds.in); kfree(mgr_ds.out);
    mgr_ds.in = ni2; mgr_ds.out = no2; mgr_ds.n = nn; return 0;
}

int ds_save_mgr(const char *path) {
    if (!mgr_ready || mgr_ds.n == 0) return -1;
    int total = mgr_ds.n * (mgr_ds.ni + mgr_ds.no);
    int needed = total * 4 + 12;
    u8 *buf = (u8*)kmalloc(needed);
    if (!buf) return -1;
    u8 *p = buf;
    *(int*)p = mgr_ds.n; p += 4;
    *(int*)p = mgr_ds.ni; p += 4;
    *(int*)p = mgr_ds.no; p += 4;
    for (int i = 0; i < mgr_ds.n; i++) {
        for (int j = 0; j < mgr_ds.ni; j++) { *(int*)p = mgr_ds.in[i * mgr_ds.ni + j]; p += 4; }
        for (int j = 0; j < mgr_ds.no; j++) { *(int*)p = mgr_ds.out[i * mgr_ds.no + j]; p += 4; }
    }
    int r = fs_write(path, buf, needed);
    kfree(buf); return r == needed ? 0 : -1;
}

int ds_mgr_count(void) { return mgr_ready ? mgr_ds.n : -1; }
void ds_clear_mgr(void) {
    if (mgr_ready) { kfree(mgr_ds.in); kfree(mgr_ds.out); }
    mgr_ready = 0; mgr_ds.n = 0;
}

int ai_export_text_ds(const char *path) {
    if (!mgr_ready || mgr_ds.n == 0) return -1;
    return ds_export_text(&mgr_ds, path);
}

int ai_import_text_ds(const char *path, int ni, int no) {
    dataset tmp;
    if (ds_import_text(&tmp, path, ni, no) != 0) return -1;
    if (mgr_ready) { kfree(mgr_ds.in); kfree(mgr_ds.out); }
    mgr_ds = tmp; mgr_ready = 1;
    return 0;
}

/* ===== WINDOW: TRAINING MONITOR ===== */
static void draw_loss_graph(int bx, int by, int w, int h, int si) {
    vga_drawrect(bx, by, w, h, 8);
    int zero = by + h - 2;
    for (int i = 1; i < LOSS_HIST; i++) {
        int p1 = (loss_pos[si] + i) % LOSS_HIST;
        int p0 = (loss_pos[si] + i - 1) % LOSS_HIST;
        int x1 = bx + 2 + (i - 1) * (w - 4) / (LOSS_HIST - 1);
        int x2 = bx + 2 + i * (w - 4) / (LOSS_HIST - 1);
        int y1 = zero - loss_hist[si][p0] * (h - 4) / 100;
        int y2 = zero - loss_hist[si][p1] * (h - 4) / 100;
        if (y1 < by) y1 = by + 2; if (y2 < by) y2 = by + 2;
        for (int t = 0; t < 8; t++) {
            int px = x1 + (x2 - x1) * t / 7;
            int py = y1 + (y2 - y1) * t / 7;
            vga_putpixel(px, py, 11);
        }
    }
}

void ai_draw(int id) {
    window_t *win = wm_get(id);
    if (!win) return;
    int bx = win->x, by = win->y;
    int si = -1;
    for (int i = 0; i < NN_SLOTS; i++)
        if (slots[i].win_id == id) { si = i; break; }
    if (si < 0) { vga_drawstring(bx + 5, by + 15, tr(S_NEURAL), 7, 1); return; }
    if (!slots[si].ready) { vga_drawstring(bx + 5, by + 15, tr(S_NONET), 7, 1); return; }

    nn *n = &slots[si].net;
    vga_drawstring(bx + 5, by + 15, tr(S_NEURAL), 10, 1);
    char dbuf[4]; itoa(si, dbuf);
    vga_drawchar(bx + 5 + strlen(tr(S_NEURAL)) * 9, by + 15, '#', 10, 1);
    vga_drawstring(bx + 14 + strlen(tr(S_NEURAL)) * 9, by + 15, dbuf, 11, 1);

    int nx[NN_MAX_L], ny[NN_MAX_L][20];
    int vw = win->w - 20;
    for (int l = 0; l < n->nl; l++) {
        nx[l] = bx + 10 + l * (vw / (n->nl > 1 ? n->nl - 1 : 1));
        int sp = n->sz[l] > 10 ? 6 : 12;
        int sy = by + 30 + (20 - n->sz[l]) * sp / 2;
        for (int ne = 0; ne < n->sz[l]; ne++) {
            ny[l][ne] = sy + ne * sp;
            fp val = n->a[l].d[ne];
            int v = FI(val * 14);
            if (v > 14) v = 14; if (v < 1) v = 1;
            u8 col = (l == n->nl - 1) ? (v > 7 ? 10 : 4) : v + 1;
            vga_drawcircle(nx[l], ny[l][ne], 3, col);
            vga_drawcircle(nx[l], ny[l][ne], 2, 0);
            vga_drawcircle(nx[l], ny[l][ne], 1, col);
        }
    }
    for (int l = 0; l < n->nl - 1; l++)
        for (int ne = 0; ne < n->sz[l]; ne++)
            for (int m = 0; m < n->sz[l + 1]; m++) {
                fp w = n->w[l].d[m * n->sz[l] + ne];
                int th = ((w < 0 ? -w : w) * 3) >> FPS;
                if (th < 1) th = 1; if (th > 3) th = 3;
                u8 col = (w < 0) ? 5 : 11;
                for (int t = 0; t < 12; t++) {
                    int x1 = nx[l], y1 = ny[l][ne];
                    int x2 = nx[l + 1], y2 = ny[l + 1][m];
                    int px = x1 + (x2 - x1) * t / 11;
                    int py = y1 + (y2 - y1) * t / 11;
                    if (t % (4 - th) == 0) vga_putpixel(px, py, col);
                }
            }

    int top_y = by + 30 + 20 * 6 + 5;
    int row = top_y;

    int lv = FI(slots[si].loss * 100 / F1);
    if (lv > 100) lv = 100; if (lv < 0) lv = 0;
    vga_drawstring(bx + 5, row, tr(S_LOSS), 7, 1);
    itoa(lv, dbuf);
    vga_drawstring(bx + 50, row, dbuf, lv < 30 ? 10 : 6, 1);

    draw_loss_graph(bx + 5, row + 9, win->w - 10, 35, si);

    row += 47;
    vga_drawstring(bx + 5, row, tr(S_STEPS), 7, 1);
    itoa(slots[si].steps, dbuf);
    vga_drawstring(bx + 50, row, dbuf, 8, 1);

    row += 10;
    vga_drawstring(bx + 5, row, tr(S_ARCH), 7, 1);
    for (int l = 0; l < n->nl; l++) {
        itoa(n->sz[l], dbuf);
        vga_drawstring(bx + 50 + l * 20, row, dbuf, 11, 1);
        if (l < n->nl - 1) vga_drawchar(bx + 50 + l * 20 + 12, row, '-', 8, 1);
    }

    row += 10;
    itoa(FI(slots[si].lr * 100), dbuf);
    vga_drawstring(bx + 5, row, tr(S_LR), 7, 1);
    vga_drawstring(bx + 35, row, dbuf, 11, 1);

    if (slots[si].ds_idx >= 0 && ds_loaded) {
        row += 10;
        vga_drawstring(bx + 5, row, tr(S_DS), 7, 1);
        vga_drawstring(bx + 25, row, ": ", 7, 1);
        itoa(ds.n, dbuf);
        vga_drawstring(bx + 30, row, dbuf, 8, 1);
        vga_drawstring(bx + 50, row, tr(S_SAMPLES), 7, 1);
    }
    if (slots[si].auto_mode) {
        row += 10;
        vga_drawstring(bx + 5, row, tr(S_AUTO_ON), 10, 1);
    }

    row = win->y + win->h - 55;
    vga_drawstring(bx + 5, row, "[T]rain [A]uto [S]ave [L]oad", 7, 1);
    row += 8;
    vga_drawstring(bx + 5, row, "[E]xprt [D]set [I]nfer [C]fg", 7, 1);
    if (slots[si].saved)
        vga_drawstring(bx + win->w - 50, by + 17, tr(S_SAVED), 10, 1);
}

void ai_keypress(int id, char c) {
    int si = -1;
    for (int i = 0; i < NN_SLOTS; i++)
        if (slots[i].win_id == id) { si = i; break; }
    if (si < 0 || !slots[si].ready) return;
    active_slot = si;

    int di = slots[si].ds_idx;
    if (c == 't' || c == 'T') {
        slots[si].auto_mode = 0;
        if (di >= 0 && ds_loaded) {
            for (int e = 0; e < slots[si].epochs; e++)
                slots[si].loss = ds_train_epoch(&slots[si].net, &ds, slots[si].lr);
            slots[si].steps += slots[si].epochs * ds.n;
            nn_fwd(&slots[si].net, ds.in);
            record_loss(si);
        }
    }
    if (c == 'a' || c == 'A') slots[si].auto_mode = !slots[si].auto_mode;
    if (c == 's' || c == 'S') {
        char p[16]; itoa(si, p);
        char path[24] = "NNMOD"; int j;
        for (j = 0; p[j]; j++) path[5 + j] = p[j];
        path[5 + j] = '.'; path[6 + j] = 'B'; path[7 + j] = 'I'; path[8 + j] = 'N'; path[9 + j] = 0;
        if (nn_save_file(&slots[si].net, path) == 0) slots[si].saved = 1;
    }
    if (c == 'l' || c == 'L') {
        char p[16]; itoa(si, p);
        char path[24] = "NNMOD"; int j;
        for (j = 0; p[j]; j++) path[5 + j] = p[j];
        path[5 + j] = '.'; path[6 + j] = 'B'; path[7 + j] = 'I'; path[8 + j] = 'N'; path[9 + j] = 0;
        nn saved;
        if (nn_load_file(&saved, path) == 0) {
            nn_free(&slots[si].net);
            slots[si].net = saved;
            slots[si].saved = 0;
            slots[si].steps = 0;
            if (di >= 0 && ds_loaded) nn_fwd(&slots[si].net, ds.in);
        }
    }
    if (c == 'e' || c == 'E') {
        char p[16]; itoa(si, p);
        char path[24] = "NNEXP"; int j;
        for (j = 0; p[j]; j++) path[5 + j] = p[j];
        path[5 + j] = '.'; path[6 + j] = 'T'; path[7 + j] = 'X'; path[8 + j] = 'T'; path[9 + j] = 0;
        if (ai_export_txt(path) == 0) slots[si].saved = 1;
    }
    if (c == 'd' || c == 'D') {
        if (ds_load(&ds, "XORDSET.BIN") == 0) {
            ds_loaded = 1;
            slot_init_net(si, 3, (int[]){ds.ni, 6, ds.no});
            slots[si].ds_idx = 0;
            slots[si].loss = F1;
            nn_fwd(&slots[si].net, ds.in);
        }
    }
    if (c == 'i' || c == 'I') {
        if (di >= 0 && ds_loaded) nn_fwd(&slots[si].net, ds.in + di * ds.ni);
    }
    if (c == 'c' || c == 'C') {
        slot_init_net(si, 3, (int[]){2, 8, 1});
    }
}

/* ===== WINDOW: NN EDITOR ===== */
void ai_editor_draw(int id) {
    window_t *win = wm_get(id);
    if (!win) return;
    int bx = win->x, by = win->y;
    int si = -1;
    for (int i = 0; i < NN_SLOTS; i++)
        if (slots[i].edit_win == id) { si = i; break; }
    if (si < 0) { vga_drawstring(bx + 5, by + 15, tr(S_NNEDIT), 10, 1); return; }

    nn *n = &slots[si].net;
    if (!slots[si].ready) { vga_drawstring(bx + 5, by + 15, tr(S_NONET), 7, 1); return; }

    vga_drawstring(bx + 5, by + 15, tr(S_NNEDIT), 10, 1);
    char buf[4]; itoa(si, buf);
    vga_drawchar(bx + 5 + strlen(tr(S_NNEDIT)) * 9, by + 15, '#', 10, 1);
    vga_drawstring(bx + 14 + strlen(tr(S_NNEDIT)) * 9, by + 15, buf, 11, 1);

    int row = by + 30;
    vga_drawstring(bx + 5, row, tr(S_LAYERS), 7, 1);
    row += 10;
    for (int l = 0; l < n->nl; l++) {
        itoa(n->sz[l], buf);
        vga_drawstring(bx + 10, row, buf, 11, 1);
        vga_drawstring(bx + 40, row, nn_act_name(n->acts[l]), 8, 1);
        row += 10;
    }

    row += 5;
    vga_drawstring(bx + 5, row, "[+L] add  [-L] remove", 7, 1);
    row += 10;
    vga_drawstring(bx + 5, row, "[N]eur+ [M]eur-", 7, 1);
    row += 10;
    vga_drawstring(bx + 5, row, "[0-7] select layer", 7, 1);
    row += 10;
    vga_drawstring(bx + 5, row, "[F] sig [R]elu [T]anh", 7, 1);
    row += 10;
    vga_drawstring(bx + 5, row, "[C]reate network", 11, 1);
}

void ai_editor_keypress(int id, char c) {
    int si = -1;
    for (int i = 0; i < NN_SLOTS; i++)
        if (slots[i].edit_win == id) { si = i; break; }
    if (si < 0 || !slots[si].ready) return;
    nn *n = &slots[si].net;
    static int sel = 0;

    if (c == '+') {
        if (n->nl >= NN_MAX_L) return;
        int newsz[NN_MAX_L + 1];
        for (int i = 0; i < n->nl; i++) newsz[i] = n->sz[i];
        newsz[n->nl] = n->sz[n->nl - 1];
        nn saved;
        slot_init_net(si, n->nl + 1, newsz);
        if (n->nl > 1) for (int i = 0; i < n->nl - 2; i++) slots[si].net.acts[i + 1] = n->acts[i + 1];
    }
    if (c == '-') {
        if (n->nl <= 2) return;
        int newsz[NN_MAX_L];
        for (int i = 0; i < n->nl - 1; i++) newsz[i] = n->sz[i];
        slot_init_net(si, n->nl - 1, newsz);
    }
    if (c == 'n' || c == 'N') {
        if (n->sz[sel] < 30) { n->sz[sel]++; slot_init_net(si, n->nl, n->sz); }
    }
    if (c == 'm' || c == 'M') {
        if (n->sz[sel] > 1) { n->sz[sel]--; slot_init_net(si, n->nl, n->sz); }
    }
    if (c >= '0' && c <= '7') {
        int l = c - '0';
        if (l < n->nl) sel = l;
    }
    if (c == 'f' || c == 'F') { n->acts[sel] = ACT_SIGMOID; }
    if (c == 'r' || c == 'R') { n->acts[sel] = ACT_RELU; }
    if (c == 't' || c == 'T') { n->acts[sel] = ACT_TANH; }
    if (c == 'c' || c == 'C') {
        slot_init_net(si, n->nl, n->sz);
    }
}

/* ===== WINDOW: DATASET VIEWER ===== */
void ai_ds_draw(int id) {
    window_t *win = wm_get(id);
    if (!win) return;
    int bx = win->x, by = win->y;
    if (!ds_loaded) { vga_drawstring(bx + 5, by + 15, tr(S_EMPTY), 7, 1); return; }

    vga_drawstring(bx + 5, by + 15, tr(S_DSVIEW), 10, 1);
    char buf[16];
    itoa(ds.n, buf);
    vga_drawstring(bx + 60, by + 15, buf, 8, 1);
    vga_drawstring(bx + 80, by + 15, tr(S_SAMPLES), 7, 1);

    static int scroll = 0;
    int max_vis = (win->h - 30) / 10;
    if (max_vis < 1) max_vis = 1;

    for (int i = 0; i < max_vis && i + scroll < ds.n; i++) {
        int si2 = i + scroll;
        int row = by + 28 + i * 10;
        vga_drawchar(bx + 5, row, '0' + (si2 / 10) % 10, 8, 1);
        vga_drawchar(bx + 9, row, '0' + si2 % 10, 8, 1);
        vga_drawstring(bx + 14, row, "I:", 7, 1);
        for (int j = 0; j < ds.ni && j < 4; j++) {
            int v = FI(ds.in[si2 * ds.ni + j]);
            itoa(v, buf);
            vga_drawstring(bx + 30 + j * 15, row, buf, 11, 1);
        }
        vga_drawstring(bx + 96, row, "O:", 7, 1);
        for (int j = 0; j < ds.no && j < 4; j++) {
            int v = FI(ds.out[si2 * ds.no + j]);
            itoa(v, buf);
            vga_drawstring(bx + 120 + j * 15, row, buf, 6, 1);
        }
    }
    vga_drawstring(bx + 5, win->y + win->h - 12, "[U]p [D]n [C]ls", 7, 1);
}

void ai_ds_keypress(int id, char c) {
    (void)id;
    static int scroll = 0;
    if (!ds_loaded) return;
    if (c == 0x80 && scroll > 0) scroll--;
    if (c == 0x81 && scroll < ds.n - 1) scroll++;
    if (c == 'u' || c == 'U') scroll = scroll > 0 ? scroll - 1 : 0;
    if (c == 'd' || c == 'D') scroll = scroll < ds.n - 1 ? scroll + 1 : scroll;
    if (c == 'c' || c == 'C') { scroll = 0; wm_close(id); }
}

/* ===== WINDOW: WEIGHT / NEURON DETAIL ===== */
static int wgt_si, wgt_sel_layer;

void ai_weights_draw(int id) {
    window_t *win = wm_get(id);
    if (!win) return;
    int bx = win->x, by = win->y;
    int si = -1;
    for (int i = 0; i < NN_SLOTS; i++)
        if (slots[i].wgt_win == id) { si = i; break; }
    if (si < 0 || !slots[si].ready) {
        vga_drawstring(bx + 5, by + 15, tr(S_NONET), 7, 1); return;
    }
    nn *n = &slots[si].net;
    char buf[16];
    vga_drawstring(bx + 5, by + 15, tr(S_WEIGHTS), 10, 1);
    itoa(si, buf);
    vga_drawchar(bx + 5 + strlen(tr(S_WEIGHTS)) * 9, by + 15, '#', 10, 1);
    vga_drawstring(bx + 14 + strlen(tr(S_WEIGHTS)) * 9, by + 15, buf, 11, 1);

    int col_off = 0;
    if (win->w > 200 && n->nl >= 2) {
        if (wgt_sel_layer >= n->nl - 1) wgt_sel_layer = n->nl - 2;
        if (wgt_sel_layer < 0) wgt_sel_layer = 0;
        int l = wgt_sel_layer;
        int max_cells = (win->w - 20) / 10;
        if (max_cells > n->sz[l] * n->sz[l + 1]) max_cells = n->sz[l] * n->sz[l + 1];

        vga_drawstring(bx + 5, by + 28, tr(S_ARCH), 7, 1);
        itoa(l, buf);
        vga_drawstring(bx + 30, by + 28, buf, 11, 1);
        vga_drawstring(bx + 40, by + 28, "-", 7, 1);
        itoa(l + 1, buf);
        vga_drawstring(bx + 55, by + 28, buf, 11, 1);
        vga_drawstring(bx + 65, by + 28, nn_act_name(n->acts[l + 1]), 8, 1);

        for (int i2 = 0; i2 < max_cells; i2++) {
            int cell_r = i2 / (n->sz[l + 1]);
            int cell_c = i2 % (n->sz[l + 1]);
            int idx = cell_c * n->sz[l] + cell_r;
            fp wval = n->w[l].d[idx];
            int fiv = FI(wval);
            int x = bx + 10 + (i2 % 12) * 12;
            int y = by + 38 + (i2 / 12) * 12;
            u8 col = fiv > 0 ? 11 : (fiv < 0 ? 5 : 8);
            vga_drawrect(x, y, 10, 10, col);
            vga_drawrect(x + 1, y + 1, 8, 8, 0);
        }
    }

    vga_drawstring(bx + 5, win->y + win->h - 12, "[<] [>] lay [W]atch", 7, 1);
}

void ai_weights_keypress(int id, char c) {
    int si = -1;
    for (int i = 0; i < NN_SLOTS; i++)
        if (slots[i].wgt_win == id) { si = i; break; }
    if (si < 0 || !slots[si].ready) return;
    nn *n = &slots[si].net;
    if (c == '<' || c == ',') {
        if (wgt_sel_layer > 0) wgt_sel_layer--;
    }
    if (c == '>' || c == '.') {
        if (wgt_sel_layer < n->nl - 2) wgt_sel_layer++;
    }
    (void)id;
}

/* ===== SHELL: WINDOW LAUNCHERS ===== */
void ai_open_trainer(int si) {
    if (si < 0 || si >= slot_count || !slots[si].ready) return;
    if (slots[si].win_id >= 0) { wm_focus(slots[si].win_id); return; }
    char title[24]; int ti = 0;
    const char *tp = tr(S_NEURAL);
    while (*tp && ti < 21) title[ti++] = *tp++;
    title[ti++] = ' ';
    title[ti++] = '#';
    itoa(si, title + ti);
    int wid = wm_create(10, 10 + si * 30, 220, 240, title, ai_draw, ai_keypress, 0);
    slots[si].win_id = wid;
}

void ai_open_editor(int si) {
    if (si < 0 || si >= slot_count || !slots[si].ready) return;
    if (slots[si].edit_win >= 0) { wm_focus(slots[si].edit_win); return; }
    char title[24]; int ti = 0;
    const char *tp = tr(S_NNEDIT);
    while (*tp && ti < 21) title[ti++] = *tp++;
    title[ti++] = ' ';
    title[ti++] = '#';
    itoa(si, title + ti);
    int wid = wm_create(250, 10 + si * 30, 180, 160, title, ai_editor_draw, ai_editor_keypress, 0);
    slots[si].edit_win = wid;
}

void ai_open_ds_view(void) {
    static int ds_win = -1;
    if (ds_win >= 0) { wm_focus(ds_win); return; }
    ds_win = wm_create(300, 100, 220, 160, tr(S_DSVIEW), ai_ds_draw, ai_ds_keypress, 0);
}

void ai_open_weights(int si) {
    if (si < 0 || si >= slot_count || !slots[si].ready) return;
    if (slots[si].wgt_win >= 0) { wm_focus(slots[si].wgt_win); return; }
    char title[24]; int ti = 0;
    const char *tp = tr(S_WEIGHTS);
    while (*tp && ti < 21) title[ti++] = *tp++;
    title[ti++] = ' ';
    title[ti++] = '#';
    itoa(si, title + ti);
    int wid = wm_create(10, 200 + si * 30, 200, 180, title, ai_weights_draw, ai_weights_keypress, 0);
    wgt_si = si;
    wgt_sel_layer = 0;
    slots[si].wgt_win = wid;
}

int ai_select_slot(int si) {
    if (si < 0 || si >= NN_SLOTS || !slots[si].ready) return -1;
    active_slot = si;
    return 0;
}

int ai_get_slot_count(void) { return slot_count; }
int ai_slot_ready(int si) {
    if (si < 0 || si >= slot_count) return 0;
    return slots[si].ready ? 1 : 0;
}
