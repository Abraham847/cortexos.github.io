#include "kernel.h"
#include "bios.h"
#include "fs.h"
#include "nn.h"
#include "ipc.h"
#include "model.h"
#include "lang.h"
#include "heap.h"

/* ai_get_info declared in kernel.h */

static char shell_buf[64];
static int shell_pos;
static char output[16][32];
static int out_lines;

#define HIST_SZ 8
static char hist[HIST_SZ][64];
static int hist_count, hist_pos, hist_idx;

static void out_add(const char *s) {
    if (out_lines < 16) {
        int i;
        for (i = 0; s[i] && i < 31; i++)
            output[out_lines][i] = s[i];
        output[out_lines][i] = 0;
        out_lines++;
    } else {
        for (int i = 0; i < 15; i++)
            for (int j = 0; j < 32; j++)
                output[i][j] = output[i + 1][j];
        int i;
        for (i = 0; s[i] && i < 31; i++)
            output[15][i] = s[i];
        output[15][i] = 0;
    }
}

static void out_int(int v) {
    char buf[16];
    itoa(v, buf);
    out_add(buf);
}

void shell_init(void) {
    shell_buf[0] = 0;
    shell_pos = 0;
    out_lines = 0;
    hist_count = 0;
    hist_pos = 0;
    hist_idx = 0;
}

static void hist_add(const char *s) {
    if (!*s) return;
    for (int i = 0; i < hist_count && i < HIST_SZ; i++)
        if (strcmp(hist[i], s) == 0) return;
    int slot = hist_count % HIST_SZ;
    int j;
    for (j = 0; s[j] && j < 63; j++) hist[slot][j] = s[j];
    hist[slot][j] = 0;
    if (hist_count < HIST_SZ) hist_count++;
    hist_idx = hist_count;
}

void shell_draw(int id) {
    window_t *win = wm_get(id);
    if (!win) return;
    int bx = win->x, by = win->y;

    vga_drawstring(bx + 5, by + 15, "Terminal", 7, 1);

    int start = out_lines > 5 ? out_lines - 5 : 0;
    int count = out_lines > 5 ? 5 : out_lines;
    for (int i = 0; i < count; i++)
        vga_drawstring(bx + 5, by + 25 + i * 10, output[start + i], 7, 1);

    vga_drawstring(bx + 5, by + 80, "> ", 6, 1);
    vga_drawstring(bx + 20, by + 80, shell_buf, 15, 1);

    static int blink;
    blink++;
    if (blink < 20)
        vga_drawchar(bx + 20 + shell_pos * 9, by + 80, '_', 11, 1);
    if (blink > 40) blink = 0;
}

static int eval_expr(const char *s) {
    int a = 0, b = 0;
    char op = 0;
    int phase = 0;
    while (*s) {
        if (*s >= '0' && *s <= '9') {
            if (phase == 0) a = a * 10 + (*s - '0');
            else b = b * 10 + (*s - '0');
        } else if (*s == '+' || *s == '-' || *s == '*' || *s == '/') {
            op = *s;
            phase = 1;
        }
        s++;
    }
    if (!op) return a;
    if (op == '+') return a + b;
    if (op == '-') return a - b;
    if (op == '*') return a * b;
    if (op == '/') return b ? a / b : 0;
    return a;
}

static int atoi_s(const char *s) {
    int v = 0, neg = 0;
    if (!s) return 0;
    while (*s == ' ') s++;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    return neg ? -v : v;
}

static char *next_arg(char *p) {
    while (*p && *p != ' ') p++;
    while (*p == ' ') p++;
    return p;
}

static char *copy_arg(char *dst, char *src, int max) {
    int i = 0;
    while (*src == ' ') src++;
    while (*src && *src != ' ' && i < max - 1) dst[i++] = *src++;
    dst[i] = 0;
    return src;
}

extern void edit_open(const char *fn);
extern void forth_open(void);

void shell_keypress(int id, char c) {
    (void)id;
    if (c >= 32 && shell_pos < 60 && c < 0x80) {
        shell_buf[shell_pos++] = c;
        shell_buf[shell_pos] = 0;
        hist_idx = hist_count;
    } else if (c == 8 && shell_pos > 0) {
        shell_buf[--shell_pos] = 0;
        hist_idx = hist_count;
    } else if (c == 13) {
        out_add(shell_buf);
        hist_add(shell_buf);

        if (strcmp(shell_buf, "HELP") == 0) {
            out_add("HELP   - this text");
            out_add("CALC   - a+b / a-b / a*b");
            out_add("INFO   - system info");
            out_add("CLEAR  - clear output");
            out_add("DIR    - list files");
            out_add("DEL x  - delete file");
            out_add("REN a b- rename file");
            out_add("EDIT x - text editor");
            out_add("FORTH  - FORTH repl");
            out_add("PAINT  - drawing app");
            out_add("NEURAL - open NN monitor");
            out_add("NNEDIT - open NN editor");
            out_add("DSVIEW - open dataset view");
            out_add("WEIGHTS- open weight view");
            out_add("NN ... - NN commands");
            out_add("DS ... - dataset mgr");
            out_add("MSG    - IPC: SEND/RECV");
            out_add("MODEL  - model mgr");
            out_add("PS     - list tasks");
            out_add("LANG   - set language (EN/ES/FR/DE/PT)");
        } else if (shell_buf[0] == 'C' && shell_buf[1] == 'A' &&
                   shell_buf[2] == 'L' && shell_buf[3] == 'C') {
            if (shell_buf[4] == ' ')
                out_int(eval_expr(shell_buf + 5));
            else
                out_add("Usage: CALC a+b");
        } else if (strcmp(shell_buf, "INFO") == 0) {
            out_add("CortexOS v1.0");
            out_add("CPU: i686 32bit PMode");
            char dbuf[16];
            itoa(vga_width, dbuf);
            out_add("Display: ");
            out_add(dbuf);
            out_add("x");
            itoa(vga_height, dbuf);
            out_add(dbuf);
            char buf[16];
            u32 mem = bios_total_mem_kb();
            itoa(mem / 1024, buf);
            out_add("RAM: ");
            out_add(buf);
            out_add("MB");
            itoa(timer_ticks / 100, buf);
            out_add("Up: ");
            out_add(buf);
            out_add("s");
        } else if (strcmp(shell_buf, "CLEAR") == 0) {
            out_lines = 0;
        } else if (shell_buf[0] == 'D' && shell_buf[1] == 'I' && shell_buf[2] == 'R') {
            fs_entry_t ents[50];
            int n = fs_list("", ents, 50);
            if (n > 0) {
                char buf[32];
                for (int i = 0; i < n && i < 14; i++) {
                    int j;
                    for (j = 0; ents[i].name[j]; j++) buf[j] = ents[i].name[j];
                    buf[j] = ' '; j++;
                    for (; j < 20; j++) buf[j] = ' ';
                    itoa(ents[i].size, buf + 12);
                    out_add(buf);
                }
            } else if (n == 0) out_add(tr(S_EMPTY));
            else out_add(tr(S_ERR));
        } else if (shell_buf[0] == 'D' && shell_buf[1] == 'E' && shell_buf[2] == 'L') {
            if (shell_buf[3] == ' ') {
                if (fs_delete(shell_buf + 4) == 0) out_add(tr(S_OK));
                else out_add(tr(S_ERR));
            } else out_add("Usage: DEL file");
        } else if (shell_buf[0] == 'R' && shell_buf[1] == 'E' && shell_buf[2] == 'N') {
            char old[20], new[20];
            int idx = 4, oi = 0, ni = 0;
            while (shell_buf[idx] == ' ') idx++;
            while (shell_buf[idx] && shell_buf[idx] != ' ' && oi < 19) old[oi++] = shell_buf[idx++];
            old[oi] = 0;
            while (shell_buf[idx] == ' ') idx++;
            while (shell_buf[idx] && ni < 19) new[ni++] = shell_buf[idx++];
            new[ni] = 0;
            if (oi && ni && fs_rename(old, new) == 0) out_add(tr(S_OK));
            else out_add(tr(S_ERR));
        } else if (strcmp(shell_buf, "NEURAL") == 0) {
            ai_open_trainer(0);
            out_add(tr(S_OK));
        } else if (strcmp(shell_buf, "NNEDIT") == 0) {
            ai_open_editor(0);
            out_add(tr(S_OK));
        } else if (strcmp(shell_buf, "DSVIEW") == 0) {
            ai_open_ds_view();
            out_add(tr(S_OK));
        } else if (shell_buf[0] == 'W' && shell_buf[1] == 'E' && shell_buf[2] == 'I' && shell_buf[3] == 'G' && shell_buf[4] == 'H' && shell_buf[5] == 'T' && shell_buf[6] == 'S') {
            int si = 0;
            if (shell_buf[7] == ' ') si = atoi_s(shell_buf + 8);
            if (si < 0 || si >= ai_get_slot_count() || !ai_slot_ready(si))
                out_add(tr(S_NOSLOT));
            else { ai_open_weights(si); out_add(tr(S_OK)); }
        } else if (shell_buf[0] == 'E' && shell_buf[1] == 'D' && shell_buf[2] == 'I' && shell_buf[3] == 'T') {
            char *fn = shell_buf[4] == ' ' ? shell_buf + 5 : 0;
            edit_open(fn);
            out_add(tr(S_EDITOR));
            out_add(" ");
            out_add(tr(S_OK));
        } else if (strcmp(shell_buf, "FORTH") == 0) {
            forth_open();
            out_add(tr(S_FORTH));
            out_add(" ");
            out_add(tr(S_OK));
        } else if (shell_buf[0] == 'P' && shell_buf[1] == 'A' && shell_buf[2] == 'I' && shell_buf[3] == 'N' && shell_buf[4] == 'T') {
            paint_open();
            out_add(tr(S_PAINT));
            out_add(" ");
            out_add(tr(S_OK));
        } else if (shell_buf[0] == 'N' && shell_buf[1] == 'N' && (shell_buf[2] == ' ' || shell_buf[2] == 0)) {
            char *p = shell_buf + 2;
            while (*p == ' ') p++;
            if (p[0] == 'C' && p[1] == 'R' && p[2] == 'E' && p[3] == 'A' && p[4] == 'T' && p[5] == 'E') {
                p = next_arg(p + 6);
                int sz[NN_MAX_L], nl = 0;
                while (*p && nl < NN_MAX_L) {
                    sz[nl++] = atoi_s(p);
                    p = next_arg(p);
                }
                if (nl >= 2) {
                    ai_create_net(nl, sz);
                    out_add("NN ");
                    out_add(tr(S_CREATED));
                } else out_add("ERR: need 2+ layers");
            } else if (p[0] == 'T' && p[1] == 'R' && p[2] == 'A' && p[3] == 'I' && p[4] == 'N') {
                p = next_arg(p + 5);
                int ep = *p ? atoi_s(p) : 100;
                p = next_arg(p);
                int lr = *p ? atoi_s(p) : 50;
                int r = ai_train_net(ep, lr);
                if (r == 0) out_add(tr(S_TRAINED));
                else if (r == -2) out_add("ERR: no dataset");
                else out_add(tr(S_ERR));
            } else if (p[0] == 'L' && p[1] == 'O' && p[2] == 'A' && p[3] == 'D') {
                p = next_arg(p + 4);
                if (*p) {
                    if (ai_load_dataset(p) == 0) {
                        out_add(tr(S_DS));
                        out_add(" ");
                        out_add(tr(S_LOADED));
                    } else out_add(tr(S_ERR));
                } else out_add("Usage: NN LOAD file");
            } else if (p[0] == 'S' && p[1] == 'A' && p[2] == 'V' && p[3] == 'E') {
                p = next_arg(p + 4);
                const char *fn = *p ? p : "NNMOD.BIN";
                if (ai_save_model(fn) == 0) {
                    out_add(tr(S_MODEL));
                    out_add(" ");
                    out_add(tr(S_SAVED));
                } else out_add(tr(S_ERR));
            } else if (p[0] == 'L' && p[1] == 'O' && p[2] == 'A' && p[3] == 'D' && p[4] == 'M' && p[5] == 'O' && p[6] == 'D') {
                p = next_arg(p + 7);
                const char *fn = *p ? p : "NNMOD.BIN";
                if (ai_load_model(fn) == 0) {
                    out_add(tr(S_MODEL));
                    out_add(" ");
                    out_add(tr(S_LOADED));
                } else out_add(tr(S_ERR));
            } else if (p[0] == 'E' && p[1] == 'X' && p[2] == 'P' && p[3] == 'O' && p[4] == 'R' && p[5] == 'T') {
                p = next_arg(p + 6);
                const char *fn = *p ? p : "NNEXPORT.TXT";
                if (ai_export_txt(fn) == 0) out_add(tr(S_EXPORTED));
                else out_add(tr(S_ERR));
            } else if (p[0] == 'I' && p[1] == 'N' && p[2] == 'F' && p[3] == 'E' && p[4] == 'R') {
                p = next_arg(p + 5);
                if (*p) {
                    char res[64];
                    int n = ai_infer_str(p, res, 64);
                    if (n > 0) out_add(res);
                    else out_add("ERR");
                } else out_add("Usage: NN INFER v1 v2 ...");
            } else if (p[0] == 'A' && p[1] == 'C' && p[2] == 'T') {
                p = next_arg(p + 3);
                int layer = atoi_s(p);
                p = next_arg(p);
                int act = atoi_s(p);
                ai_set_act(layer, act);
                out_add(tr(S_ACT_SET));
            } else if (p[0] == 'A' && p[1] == 'U' && p[2] == 'T' && p[3] == 'O') {
                p = next_arg(p + 4);
                int on = (*p == '1' || *p == 'o' || *p == 'O') ? 1 : 0;
                ai_set_auto(on);
                out_add(on ? tr(S_AUTO_ON) : tr(S_AUTO_OFF));
            } else if (p[0] == 'S' && p[1] == 'L' && p[2] == 'O' && p[3] == 'T') {
                p = next_arg(p + 4);
                int si = atoi_s(p);
                if (ai_select_slot(si) == 0) out_add(tr(S_SLOT_SEL));
                else out_add(tr(S_ERR));
            } else if (p[0] == 'S' && p[1] == 'A' && p[2] == 'V' && p[3] == 'E' && p[4] == 'S') {
                p = next_arg(p + 5);
                int si = atoi_s(p); p = next_arg(p);
                if (ai_save_model_slot(si, *p ? p : "NNMOD.BIN") == 0) out_add(tr(S_SAVED));
                else out_add(tr(S_ERR));
            } else if (p[0] == 'L' && p[1] == 'O' && p[2] == 'A' && p[3] == 'D' && p[4] == 'S') {
                p = next_arg(p + 5);
                int si = atoi_s(p); p = next_arg(p);
                if (ai_load_model_slot(si, *p ? p : "NNMOD.BIN") == 0) out_add(tr(S_LOADED));
                else out_add(tr(S_ERR));
            } else if (p[0] == 'I' && p[1] == 'N' && p[2] == 'F' && p[3] == 'O') {
                char buf[80];
                ai_get_info(buf, 80);
                out_add(buf);
            } else {
                out_add("NN: CREATE TRAIN LOAD SAVE LOADMOD EXPORT INFER ACT AUTO INFO SLOT SAVES LOADS");
            }
        } else if (shell_buf[0] == 'D' && shell_buf[1] == 'S' && (shell_buf[2] == ' ' || shell_buf[2] == 0)) {
            char *p = shell_buf + 2;
            while (*p == ' ') p++;
            if (p[0] == 'C' && p[1] == 'R' && p[2] == 'E' && p[3] == 'A' && p[4] == 'T' && p[5] == 'E') {
                p = next_arg(p + 6);
                int ni = atoi_s(p); p = next_arg(p);
                int no = atoi_s(p);
                if (ni > 0 && no > 0 && ds_create_mgr(ni, no) == 0)
                    out_add(tr(S_DS_READY));
                else out_add(tr(S_ERR));
            } else if (p[0] == 'A' && p[1] == 'D' && p[2] == 'D') {
                p = next_arg(p + 3);
                if (*p) {
                    int r = ds_add_sample(p);
                    if (r == 0) {
                        char buf[16];
                        itoa(ds_mgr_count(), buf);
                        out_add(tr(S_SAMPLE_ADD));
                        out_add(buf);
                    } else if (r == -2) out_add("ERR: wrong dim");
                    else out_add(tr(S_ERR));
                } else out_add("Usage: DS ADD v1 v2 .. o1 o2 ..");
            } else if (p[0] == 'S' && p[1] == 'A' && p[2] == 'V' && p[3] == 'E') {
                p = next_arg(p + 4);
                if (*p) {
                    if (ds_save_mgr(p) == 0) {
                        out_add(tr(S_DS_SAVED));
                        ds_clear_mgr();
                    } else out_add(tr(S_ERR));
                } else out_add("Usage: DS SAVE file.bin");
            } else if (p[0] == 'C' && p[1] == 'L' && p[2] == 'E' && p[3] == 'A' && p[4] == 'R') {
                ds_clear_mgr();
                out_add(tr(S_DS_CLR));
            } else if (p[0] == 'E' && p[1] == 'X' && p[2] == 'P' && p[3] == 'O' && p[4] == 'R' && p[5] == 'T') {
                p = next_arg(p + 6);
                if (*p) {
                    if (ai_export_text_ds(p) == 0)                     out_add(tr(S_EXPORTED));
                    else out_add(tr(S_ERR));
                } else out_add("Usage: DS EXPORT path.txt");
            } else if (p[0] == 'I' && p[1] == 'M' && p[2] == 'P' && p[3] == 'O' && p[4] == 'R' && p[5] == 'T') {
                p = next_arg(p + 6);
                char dspath[32]; p = copy_arg(dspath, p, 20);
                int ni2 = atoi_s(p); p = next_arg(p);
                int no2 = atoi_s(p);
                if (ni2 > 0 && no2 > 0) {
                    if (ai_import_text_ds(dspath, ni2, no2) == 0) out_add(tr(S_LOADED));
                    else out_add(tr(S_ERR));
                } else out_add("Usage: DS IMPORT path.txt ni no");
            } else if (p[0] == 'S' && p[1] == 'T' && p[2] == 'A' && p[3] == 'T') {
                int n = ds_mgr_count();
                char buf[16];
                itoa(n, buf);
                out_add(tr(S_DS));
                out_add(" ");
                out_add(tr(S_SAMPLES));
                out_add(":");
                out_add(buf);
            } else {
                out_add("DS: CREATE ADD SAVE EXPORT IMPORT CLEAR STAT");
            }
        } else if (shell_buf[0] == 'P' && shell_buf[1] == 'S') {
            char buf[16];
            itoa(task_count(), buf);
            out_add(tr(S_PS));
            out_add(buf);
            out_add("NN:");
            itoa(ai_get_slot_count(), buf);
            out_add(buf);
            for (int si = 0; si < ai_get_slot_count(); si++) {
                if (ai_slot_ready(si)) {
                    char sb[24]; int sp = 0;
                    itoa(si, sb); sp = strlen(sb);
                    sb[sp++] = ':'; sb[sp++] = ' '; sb[sp] = 0;
                    out_add(sb);
                }
            }
        } else if (shell_buf[0] == 'M' && shell_buf[1] == 'S' && shell_buf[2] == 'G' && (shell_buf[3] == ' ' || shell_buf[3] == 0)) {
            char *p = shell_buf + 3;
            while (*p == ' ') p++;
            if (p[0] == 'S' && p[1] == 'E' && p[2] == 'N' && p[3] == 'D') {
                p = next_arg(p + 4);
                int dst = atoi_s(p); p = next_arg(p);
                if (dst >= 0 && *p) {
                    if (ipc_send(dst, 1, p, strlen(p)) == 0) out_add(tr(S_SENT));
                    else out_add("ERR: full or bad dst");
                } else out_add("Usage: MSG SEND dst text");
            } else if (p[0] == 'R' && p[1] == 'E' && p[2] == 'C' && p[3] == 'V') {
                char rbuf[IPC_DATA_SZ + 1]; int rtype, rlen;
                int src = ipc_recv(-1, &rtype, rbuf, &rlen);
                if (src >= 0) {
                    rbuf[rlen] = 0;
                    char it[8]; itoa(src, it);
                    out_add(it);
                    out_add(": ");
                    out_add(rbuf);
                } else out_add(tr(S_NOMSG));
            } else if (p[0] == 'C' && p[1] == 'H' && p[2] == 'E' && p[3] == 'C' && p[4] == 'K') {
                int n = ipc_available(-1);
                char it[8]; itoa(n, it); out_add(tr(S_PENDING)); out_add(it);
            } else out_add("MSG: SEND dst text | RECV | CHECK");
        } else if (shell_buf[0] == 'M' && shell_buf[1] == 'O' && shell_buf[2] == 'D' && shell_buf[3] == 'E' && shell_buf[4] == 'L' && (shell_buf[5] == ' ' || shell_buf[5] == 0)) {
            char *p = shell_buf + 5;
            while (*p == ' ') p++;
            if (p[0] == 'R' && p[1] == 'E' && p[2] == 'G') {
                p = next_arg(p + 3);
                char mname[24]; p = copy_arg(mname, p, 16);
                if (*p) {
                    nn *net = (nn*)kmalloc(sizeof(nn)); if (!net) return;
                    int sz[4]; int nl = 0;
                    while (*p && nl < 4) { sz[nl++] = atoi_s(p); p = next_arg(p); }
                    if (nl >= 2) {
                        nn_init(net, nl, sz);
                        nn_rand(net, FF(2));
                        if (model_register(mname, net) == 0) out_add(tr(S_OK));
                        else { nn_free(net); kfree(net); out_add(tr(S_ERR)); }
                    } else { kfree(net); out_add("Usage: MODEL REG name n1 n2 ..."); }
                } else out_add("Usage: MODEL REG name n1 n2 ...");
            } else if (p[0] == 'L' && p[1] == 'O' && p[2] == 'A' && p[3] == 'D') {
                p = next_arg(p + 4);
                char mname[24]; p = copy_arg(mname, p, 16);
                char mpath[24]; p = copy_arg(mpath, p, 20);
                if (model_load_file(*mname ? mname : "model", *mpath ? mpath : "NNMOD.BIN") == 0)
                    out_add(tr(S_LOADED));
                else out_add(tr(S_ERR));
            } else if (p[0] == 'G' && p[1] == 'E' && p[2] == 'T') {
                p = next_arg(p + 3);
                char mname[24]; copy_arg(mname, p, 16);
                nn *n = model_get(mname);
                if (n) {
                    char ob[64]; ai_get_info(ob, 64);
                    out_add(ob);
                } else out_add("ERR");
            } else if (p[0] == 'P' && p[1] == 'U' && p[2] == 'T') {
                p = next_arg(p + 3);
                if (model_put(p) >= 0) out_add(tr(S_RELEASED));
                else out_add(tr(S_ERR));
            } else if (p[0] == 'U' && p[1] == 'N' && p[2] == 'R' && p[3] == 'E' && p[4] == 'G') {
                p = next_arg(p + 5);
                if (model_unregister(p) == 0) out_add(tr(S_UNREG));
                else out_add("ERR: has refs or not found");
            } else if (p[0] == 'L' && p[1] == 'I' && p[2] == 'S' && p[3] == 'T') {
                char lb[128]; model_list(lb, 128);
                out_add(lb);
                char it[16]; itoa(model_count(), it);
                out_add(tr(S_MODEL));
                out_add(":");
                out_add(it);
            } else out_add("MODEL: REG LOAD GET PUT UNREG LIST");
        } else if (shell_buf[0] == 'L' && shell_buf[1] == 'A' && shell_buf[2] == 'N' && shell_buf[3] == 'G') {
            int nl = LANG_EN;
            char lc = shell_buf[5];
            if (lc == 'E') nl = LANG_EN;
            else if (lc == 'S' || lc == 's') nl = LANG_ES;
            else if (lc == 'F' || lc == 'f') nl = LANG_FR;
            else if (lc == 'D' || lc == 'd') nl = LANG_DE;
            else if (lc == 'P' || lc == 'p') nl = LANG_PT;
            lang_set((lang_t)nl);
            out_add(tr(S_OK));
        } else if (shell_buf[0]) {
            out_add(tr(S_UNKNOWN));
        }

        shell_buf[0] = 0;
        shell_pos = 0;
    } else if (c == 0x80 || c == 0x81) {  /* up/down arrows */
        if (c == 0x80 && hist_idx > 0) hist_idx--;
        else if (c == 0x81 && hist_idx < hist_count) hist_idx++;
        if (hist_idx < hist_count) {
            int si = hist_idx % HIST_SZ;
            for (shell_pos = 0; hist[si][shell_pos]; shell_pos++)
                shell_buf[shell_pos] = hist[si][shell_pos];
            shell_buf[shell_pos] = 0;
        } else {
            shell_buf[0] = 0;
            shell_pos = 0;
        }
    }
}
