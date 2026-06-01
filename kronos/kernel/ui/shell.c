#include "kernel.h"
#include "bios.h"
#include "fs.h"
#include "nn.h"
#include "ipc.h"
#include "model.h"
#include "lang.h"
#include "heap.h"
#include "kernel_api.h"
#include "loader.h"
#include "sysmon.h"
#include "fman.h"

/* ai_get_info declared in kernel.h */

static char shell_buf[80];
static int shell_pos;
#define OUT_LINES 32
#define OUT_COLS 48
static char output[OUT_LINES][OUT_COLS];
static int out_lines;

#define HIST_SZ 8
static char hist[HIST_SZ][80];
static int hist_count, hist_pos, hist_idx;

static void out_add(const char *s) {
    if (out_lines < OUT_LINES) {
        int i;
        for (i = 0; s[i] && i < OUT_COLS - 1; i++)
            output[out_lines][i] = s[i];
        output[out_lines][i] = 0;
        out_lines++;
    } else {
        for (int i = 0; i < OUT_LINES - 1; i++)
            for (int j = 0; j < OUT_COLS; j++)
                output[i][j] = output[i + 1][j];
        int i;
        for (i = 0; s[i] && i < OUT_COLS - 1; i++)
            output[OUT_LINES - 1][i] = s[i];
        output[OUT_LINES - 1][i] = 0;
    }
}

/* user command table forward declarations */
static void ucmd_init(void);
static int ucmd_add(const char *name, const char *action);
static int ucmd_del(const char *name);
static int ucmd_save(const char *path);
static int ucmd_load(const char *path);
static void cmd_lock_init(void);
static int cmd_unlock(const char *p);
static void cmd_lock(void);
static void users_init(void);
static int users_new(const char *name, const char *pass);
static int users_login(const char *name, const char *pass);
static void users_logout(void);
static void users_save(const char *path);
static void users_load(const char *path);
static void user_cmd_path(char *out, const char *user, int max);
static void eval_cmd(const char *buf);
static int eval_depth = 0;
#define EVAL_MAX_DEPTH 8

/* user account data (declared here so shell_draw can see it) */
#define USER_MAX 8
#define USER_NAME_SZ 16
#define USER_PASS_SZ 16
static char current_user[USER_NAME_SZ];

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
    ucmd_init();
    cmd_lock_init();
    users_init();
    users_load("USERS.TXT");
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

    int vis = (win->h - 35) / 10;
    if (vis < 1) vis = 1;
    int start = out_lines > vis ? out_lines - vis : 0;
    int count = out_lines > vis ? vis : out_lines;
    for (int i = 0; i < count; i++)
        vga_drawstring(bx + 5, by + 25 + i * 10, output[start + i], 7, 1);

    int py = by + 25 + count * 10 + 5;
    int prompt_x = 5;
    if (current_user[0]) {
        vga_drawstring(bx + 5, py, current_user, 6, 1);
        prompt_x += strlen(current_user);
        vga_drawchar(bx + prompt_x, py, '>', 6, 1); prompt_x++;
    }
    vga_drawchar(bx + prompt_x, py, '>', 6, 1); prompt_x++;
    vga_drawchar(bx + prompt_x, py, ' ', 6, 1); prompt_x++;
    vga_drawstring(bx + prompt_x, py, shell_buf, 15, 1);

    static int blink;
    blink++;
    if (blink < 20)
        vga_drawchar(bx + prompt_x + shell_pos * 9, py, '_', 11, 1);
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

/* --- user-defined command aliases --- */
#define UCMD_MAX 16
#define UCMD_NAME 16
#define UCMD_ACT 60

/* password protection for user cmd management (reads from PASS.SYS) */
static int cmd_locked;

static char users_name[USER_MAX][USER_NAME_SZ];
static char users_pass[USER_MAX][USER_PASS_SZ];
static int users_count;

static void users_init(void) {
    users_count = 0;
    current_user[0] = 0;
}

static int users_new(const char *name, const char *pass) {
    if (users_count >= USER_MAX) return -1;
    for (int ci = 0; ci < users_count; ci++) {
        int eq = 1;
        for (int cj = 0; users_name[ci][cj] || name[cj]; cj++)
            if (users_name[ci][cj] != name[cj]) { eq = 0; break; }
        if (eq) return -2; /* duplicate */
    }
    int i, j;
    for (i = 0; name[i] && i < USER_NAME_SZ - 1; i++)
        users_name[users_count][i] = name[i];
    users_name[users_count][i] = 0;
    for (j = 0; pass[j] && j < USER_PASS_SZ - 1; j++)
        users_pass[users_count][j] = pass[j];
    users_pass[users_count][j] = 0;
    users_count++;
    return 0;
}

static int users_login(const char *name, const char *pass) {
    for (int i = 0; i < users_count; i++) {
        int ni, pi;
        for (ni = 0; users_name[i][ni] && name[ni] && users_name[i][ni] == name[ni]; ni++);
        if (users_name[i][ni] || name[ni]) continue;
        for (pi = 0; users_pass[i][pi] && pass[pi] && users_pass[i][pi] == pass[pi]; pi++);
        if (users_pass[i][pi] || pass[pi]) continue;
        int j;
        for (j = 0; users_name[i][j]; j++) current_user[j] = users_name[i][j];
        current_user[j] = 0;
        return 0;
    }
    return -1;
}

static void users_logout(void) {
    current_user[0] = 0;
}

static void users_save(const char *path) {
    char buf[512];
    int pos = 0;
    for (int i = 0; i < users_count; i++) {
        if (pos > 500) break;
        int j;
        for (j = 0; users_name[i][j] && pos < 510; j++) buf[pos++] = users_name[i][j];
        if (pos < 511) buf[pos++] = ':';
        for (j = 0; users_pass[i][j] && pos < 510; j++) buf[pos++] = users_pass[i][j];
        if (pos < 511) { buf[pos++] = 13; buf[pos++] = 10; }
    }
    if (pos > 0) fs_write(path, buf, pos);
}

static void users_load(const char *path) {
    char buf[512];
    int n = fs_read(path, buf, 512);
    if (n <= 0) return;
    buf[n] = 0;
    int pos = 0;
    users_count = 0;
    while (buf[pos] && users_count < USER_MAX) {
        int ni = 0;
        while (buf[pos] && buf[pos] != ':' && ni < USER_NAME_SZ - 1)
            users_name[users_count][ni++] = buf[pos++];
        if (buf[pos] == ':') pos++;
        int pi = 0;
        while (buf[pos] && buf[pos] != 13 && buf[pos] != 10 && pi < USER_PASS_SZ - 1)
            users_pass[users_count][pi++] = buf[pos++];
        users_name[users_count][ni] = 0;
        users_pass[users_count][pi] = 0;
        if (ni > 0) users_count++;
        while (buf[pos] == 13 || buf[pos] == 10) pos++;
    }
}

/* build per-user cmd path from username */
static void user_cmd_path(char *out, const char *user, int max) {
    int i = 0;
    out[i++] = 'C'; out[i++] = 'M'; out[i++] = 'D'; out[i++] = 'S'; out[i++] = '_';
    for (int j = 0; user[j] && i < max - 5; j++) out[i++] = user[j];
    out[i++] = '.'; out[i++] = 'T'; out[i++] = 'X'; out[i++] = 'T';
    out[i] = 0;
}

static void cmd_lock_init(void) {
    char stored[16] = {0};
    int n = fs_read("PASS.SYS", (u8*)stored, 15);
    cmd_locked = n > 0;
}
static int cmd_unlock(const char *p) {
    char stored[16] = {0};
    int n = fs_read("PASS.SYS", (u8*)stored, 15);
    if (n <= 0) { cmd_locked = 0; return 0; }
    int i = 0;
    while (i < 15 && stored[i] && stored[i] != 13 && stored[i] != 10) i++;
    stored[i] = 0;
    int j;
    for (j = 0; stored[j] && p[j] && stored[j] == p[j]; j++);
    if (!stored[j] && !p[j]) { cmd_locked = 0; return 0; }
    return -1;
}
static void cmd_lock(void) { cmd_locked = 1; }

static char ucmd_name[UCMD_MAX][UCMD_NAME];
static char ucmd_act[UCMD_MAX][UCMD_ACT];
static int ucmd_count;

static void ucmd_init(void) {
    ucmd_count = 0;
}

static int ucmd_add(const char *name, const char *action) {
    if (ucmd_count >= UCMD_MAX) return -1;
    int i;
    for (i = 0; name[i] && i < UCMD_NAME - 1; i++)
        ucmd_name[ucmd_count][i] = name[i];
    ucmd_name[ucmd_count][i] = 0;
    for (i = 0; action[i] && i < UCMD_ACT - 1; i++)
        ucmd_act[ucmd_count][i] = action[i];
    ucmd_act[ucmd_count][i] = 0;
    ucmd_count++;
    return 0;
}

static int ucmd_del(const char *name) {
    for (int i = 0; i < ucmd_count; i++) {
        if (strcmp(ucmd_name[i], name) == 0) {
            for (int j = i; j < ucmd_count - 1; j++) {
                int k;
                for (k = 0; ucmd_name[j + 1][k]; k++) ucmd_name[j][k] = ucmd_name[j + 1][k];
                ucmd_name[j][k] = 0;
                for (k = 0; ucmd_act[j + 1][k]; k++) ucmd_act[j][k] = ucmd_act[j + 1][k];
                ucmd_act[j][k] = 0;
            }
            ucmd_count--;
            return 0;
        }
    }
    return -1;
}

static int ucmd_save(const char *path) {
    char buf[512];
    int pos = 0;
    for (int i = 0; i < ucmd_count; i++) {
        if (pos > 500) break;
        int j;
        for (j = 0; ucmd_name[i][j] && pos < 510; j++) buf[pos++] = ucmd_name[i][j];
        if (pos < 511) buf[pos++] = ':';
        for (j = 0; ucmd_act[i][j] && pos < 510; j++) buf[pos++] = ucmd_act[i][j];
        if (pos < 511) { buf[pos++] = 13; buf[pos++] = 10; }
    }
    if (pos > 0) fs_write(path, buf, pos);
    return 0;
}

static int ucmd_load(const char *path) {
    char buf[512];
    int n = fs_read(path, buf, 512);
    if (n <= 0) return -1;
    buf[n] = 0;
    int pos = 0;
    ucmd_count = 0;
    while (buf[pos] && ucmd_count < UCMD_MAX) {
        int ni = 0;
        while (buf[pos] && buf[pos] != ':' && ni < UCMD_NAME - 1)
            ucmd_name[ucmd_count][ni++] = buf[pos++];
        if (buf[pos] == ':') pos++;
        int ai = 0;
        while (buf[pos] && buf[pos] != 13 && buf[pos] != 10 && ai < UCMD_ACT - 1)
            ucmd_act[ucmd_count][ai++] = buf[pos++];
        ucmd_name[ucmd_count][ni] = 0;
        ucmd_act[ucmd_count][ai] = 0;
        if (ni > 0) ucmd_count++;
        while (buf[pos] == 13 || buf[pos] == 10) pos++;
    }
    return 0;
}

/* evaluate a command string (same logic as the main dispatch) */
static void eval_cmd(const char *buf) {
    if (eval_depth >= EVAL_MAX_DEPTH) { out_add("ERR: depth"); eval_depth = 0; return; }
    eval_depth++;
    if (!buf || !*buf) return;
    if (strcmp(buf, "HELP") == 0) {
        out_add("HELP   - this text");
        out_add("CALC   - a+b / a-b / a*b");
        out_add("INFO   - system info");
        out_add("CLEAR  - clear output");
        out_add("DIR    - list files");
        out_add("DEL x  - delete file");
        out_add("REN a b- rename file");
        out_add("EDIT x - text editor");
        out_add("ECHO x - print text");
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
        out_add("CMD    - user cmd mgr");
        out_add("USER   - account mgr");
        out_add("PS     - list tasks");
        out_add("LANG   - set language (EN/ES/FR/DE/PT)");
    } else if (buf[0] == 'C' && buf[1] == 'A' &&
               buf[2] == 'L' && buf[3] == 'C') {
        if (buf[4] == ' ')
            out_int(eval_expr(buf + 5));
        else
            out_add("Usage: CALC a+b");
    } else if (buf[0] == 'E' && buf[1] == 'C' && buf[2] == 'H' && buf[3] == 'O') {
        if (buf[4] == ' ') out_add(buf + 5);
        else out_add("");
    } else if (strcmp(buf, "INFO") == 0) {
        out_add("CortexOS v1.0");
        out_add("CPU: i686 32bit PMode");
        char dbuf[16];
        itoa(vga_width, dbuf);
        out_add("Display: ");
        out_add(dbuf);
        out_add("x");
        itoa(vga_height, dbuf);
        out_add(dbuf);
        char buf2[16];
        u32 mem = bios_total_mem_kb();
        itoa(mem / 1024, buf2);
        out_add("RAM: ");
        out_add(buf2);
        out_add("MB");
        itoa(timer_ticks / 100, buf2);
        out_add("Up: ");
        out_add(buf2);
        out_add("s");
    } else if (strcmp(buf, "CLEAR") == 0) {
        out_lines = 0;
    } else if (strcmp(buf, "SYSMON") == 0) {
        extern void sysmon_open(void);
        sysmon_open();
        out_add(tr(S_OK));
    } else if (strcmp(buf, "FMAN") == 0) {
        extern void fman_open(void);
        fman_open();
        out_add(tr(S_OK));
    } else if (buf[0] == 'D' && buf[1] == 'I' && buf[2] == 'R') {
        fs_entry_t ents[50];
        int n = fs_list("", ents, 50);
        if (n > 0) {
            char dbuf[32];
            for (int i = 0; i < n && i < 14; i++) {
                int j;
                for (j = 0; ents[i].name[j]; j++) dbuf[j] = ents[i].name[j];
                dbuf[j] = ' '; j++;
                for (; j < 20; j++) dbuf[j] = ' ';
                itoa(ents[i].size, dbuf + 12);
                out_add(dbuf);
            }
        } else if (n == 0) out_add(tr(S_EMPTY));
        else out_add(tr(S_ERR));
    } else if (buf[0] == 'D' && buf[1] == 'E' && buf[2] == 'L') {
        if (buf[3] == ' ') {
            if (fs_delete(buf + 4) == 0) out_add(tr(S_OK));
            else out_add(tr(S_ERR));
        } else out_add("Usage: DEL file");
    } else if (buf[0] == 'R' && buf[1] == 'E' && buf[2] == 'N') {
        char old[20], new[20];
        int idx = 4, oi = 0, ni = 0;
        while (buf[idx] == ' ') idx++;
        while (buf[idx] && buf[idx] != ' ' && oi < 19) old[oi++] = buf[idx++];
        old[oi] = 0;
        while (buf[idx] == ' ') idx++;
        while (buf[idx] && ni < 19) new[ni++] = buf[idx++];
        new[ni] = 0;
        if (oi && ni && fs_rename(old, new) == 0) out_add(tr(S_OK));
        else out_add(tr(S_ERR));
    } else if (strcmp(buf, "NEURAL") == 0) {
        ai_open_trainer(0);
        out_add(tr(S_OK));
    } else if (strcmp(buf, "NNEDIT") == 0) {
        ai_open_editor(0);
        out_add(tr(S_OK));
    } else if (strcmp(buf, "DSVIEW") == 0) {
        ai_open_ds_view();
        out_add(tr(S_OK));
    } else if (buf[0] == 'W' && buf[1] == 'E' && buf[2] == 'I' && buf[3] == 'G' && buf[4] == 'H' && buf[5] == 'T' && buf[6] == 'S') {
        int si = 0;
        if (buf[7] == ' ') si = atoi_s(buf + 8);
        if (si < 0 || si >= ai_get_slot_count() || !ai_slot_ready(si))
            out_add(tr(S_NOSLOT));
        else { ai_open_weights(si); out_add(tr(S_OK)); }
    } else if (buf[0] == 'E' && buf[1] == 'D' && buf[2] == 'I' && buf[3] == 'T') {
        const char *fn = buf[4] == ' ' ? buf + 5 : 0;
        edit_open(fn);
        out_add(tr(S_EDITOR));
        out_add(" ");
        out_add(tr(S_OK));
    } else if (strcmp(buf, "FORTH") == 0) {
        forth_open();
        out_add(tr(S_FORTH));
        out_add(" ");
        out_add(tr(S_OK));
    } else if (buf[0] == 'P' && buf[1] == 'A' && buf[2] == 'I' && buf[3] == 'N' && buf[4] == 'T') {
        if (app_load("PAINT.BIN", &kernel_api) == 0) {
            out_add(tr(S_PAINT));
            out_add(" ");
            out_add(tr(S_OK));
        } else { paint_open(); out_add(tr(S_PAINT)); out_add(" "); out_add(tr(S_OK)); }
    } else if (buf[0] == 'N' && buf[1] == 'N' && (buf[2] == ' ' || buf[2] == 0)) {
        char *p = (char*)(buf + 2);
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
            char ibuf[80];
            ai_get_info(ibuf, 80);
            out_add(ibuf);
        } else {
            out_add("NN: CREATE TRAIN LOAD SAVE LOADMOD EXPORT INFER ACT AUTO INFO SLOT SAVES LOADS");
        }
    } else if (buf[0] == 'D' && buf[1] == 'S' && (buf[2] == ' ' || buf[2] == 0)) {
        char *p = (char*)(buf + 2);
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
                    char cbuf[16];
                    itoa(ds_mgr_count(), cbuf);
                    out_add(tr(S_SAMPLE_ADD));
                    out_add(cbuf);
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
                if (ai_export_text_ds(p) == 0) out_add(tr(S_EXPORTED));
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
            char sbuf[16];
            itoa(n, sbuf);
            out_add(tr(S_DS));
            out_add(" ");
            out_add(tr(S_SAMPLES));
            out_add(":");
            out_add(sbuf);
        } else {
            out_add("DS: CREATE ADD SAVE EXPORT IMPORT CLEAR STAT");
        }
    } else if (buf[0] == 'P' && buf[1] == 'S') {
        char pbuf[16];
        itoa(task_count(), pbuf);
        out_add(tr(S_PS));
        out_add(pbuf);
        out_add("NN:");
        itoa(ai_get_slot_count(), pbuf);
        out_add(pbuf);
        for (int si = 0; si < ai_get_slot_count(); si++) {
            if (ai_slot_ready(si)) {
                char sb[24]; int sp = 0;
                itoa(si, sb); sp = strlen(sb);
                sb[sp++] = ':'; sb[sp++] = ' '; sb[sp] = 0;
                out_add(sb);
            }
        }
    } else if (buf[0] == 'M' && buf[1] == 'S' && buf[2] == 'G' && (buf[3] == ' ' || buf[3] == 0)) {
        char *p = (char*)(buf + 3);
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
    } else if (buf[0] == 'M' && buf[1] == 'O' && buf[2] == 'D' && buf[3] == 'E' && buf[4] == 'L' && (buf[5] == ' ' || buf[5] == 0)) {
        char *p = (char*)(buf + 5);
        while (*p == ' ') p++;
        if (p[0] == 'R' && p[1] == 'E' && p[2] == 'G') {
            p = next_arg(p + 3);
            char mname[24]; p = copy_arg(mname, p, 16);
            if (*p) {
                nn *net = (nn*)kmalloc(sizeof(nn)); if (!net) return;
                int sz[4]; int nl = 0;
                while (*p && nl < 4) { sz[nl++] = atoi_s(p); p = next_arg(p); }
                if (nl >= 2) {
                    if (nn_init(net, nl, sz)) { kfree(net); out_add(tr(S_ERR)); return; }
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
                model_put(mname);
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
    } else if (buf[0] == 'C' && buf[1] == 'M' && buf[2] == 'D' && (buf[3] == ' ' || buf[3] == 0)) {
        char *p = (char*)(buf + 3);
        while (*p == ' ') p++;
        if (p[0] == 'U' && p[1] == 'N' && p[2] == 'L' && p[3] == 'O' && p[4] == 'C' && p[5] == 'K') {
            p = next_arg(p + 6);
            if (cmd_unlock(p) == 0) out_add(tr(S_OK));
            else out_add("ERR: wrong password");
        } else if (p[0] == 'L' && p[1] == 'O' && p[2] == 'C' && p[3] == 'K') {
            cmd_lock();
            out_add(tr(S_OK));
        } else if (cmd_locked) {
            out_add("Locked. Use: CMD UNLOCK <password>");
        } else if (p[0] == 'N' && p[1] == 'E' && p[2] == 'W') {
            p = next_arg(p + 3);
            char cn[UCMD_NAME]; p = copy_arg(cn, p, UCMD_NAME);
            if (*p) {
                if (ucmd_add(cn, p) == 0) {
                    out_add("CMD ");
                    out_add(cn);
                    out_add(" ");
                    out_add(tr(S_CREATED));
                } else out_add("ERR: max commands reached");
            } else out_add("Usage: CMD NEW name action");
        } else if (p[0] == 'D' && p[1] == 'E' && p[2] == 'L') {
            p = next_arg(p + 3);
            if (ucmd_del(p) == 0) out_add(tr(S_OK));
            else out_add("ERR: not found");
        } else if (p[0] == 'L' && p[1] == 'I' && p[2] == 'S' && p[3] == 'T') {
            if (ucmd_count == 0) { out_add("No user commands"); return; }
            for (int i = 0; i < ucmd_count; i++) {
                char lb[UCMD_NAME + UCMD_ACT + 4];
                int j;
                for (j = 0; ucmd_name[i][j]; j++) lb[j] = ucmd_name[i][j];
                for (; j < 16; j++) lb[j] = ' ';
                lb[16] = '=';
                lb[17] = ' ';
                int k;
                for (k = 0; ucmd_act[i][k]; k++) lb[18 + k] = ucmd_act[i][k];
                lb[18 + k] = 0;
                out_add(lb);
            }
        } else if (p[0] == 'S' && p[1] == 'A' && p[2] == 'V' && p[3] == 'E') {
            p = next_arg(p + 4);
            if (*p) { ucmd_save(p); out_add(tr(S_SAVED)); }
            else if (current_user[0]) {
                char upath[32]; user_cmd_path(upath, current_user, 32);
                ucmd_save(upath); out_add(tr(S_SAVED));
            } else out_add("Usage: CMD SAVE [path]");
        } else if (p[0] == 'L' && p[1] == 'O' && p[2] == 'A' && p[3] == 'D') {
            p = next_arg(p + 4);
            const char *loadpath = "CMDS.TXT";
            char upath[32];
            if (*p) loadpath = p;
            else if (current_user[0]) { user_cmd_path(upath, current_user, 32); loadpath = upath; }
            if (ucmd_load(loadpath) == 0) out_add(tr(S_LOADED));
            else out_add(tr(S_ERR));
        } else if (p[0] == 'U' && p[1] == 'S' && p[2] == 'E' && p[3] == 'R' && p[4] == 'S') {
            p = next_arg(p + 5);
            users_save(*p ? p : "USERS.TXT");
            out_add(tr(S_SAVED));
        } else {
            out_add("CMD: UNLOCK <pw> | LOCK | NEW name action | DEL name | LIST | SAVE [file] | LOAD [file] | USERS [file]");
        }
    } else if (buf[0] == 'U' && buf[1] == 'S' && buf[2] == 'E' && buf[3] == 'R' && (buf[4] == ' ' || buf[4] == 0)) {
        char *p = (char*)(buf + 4);
        while (*p == ' ') p++;
        if (p[0] == 'L' && p[1] == 'O' && p[2] == 'G' && p[3] == 'I' && p[4] == 'N') {
            p = next_arg(p + 5);
            char un[USER_NAME_SZ]; p = copy_arg(un, p, USER_NAME_SZ);
            char pw[USER_PASS_SZ]; copy_arg(pw, p, USER_PASS_SZ);
            if (users_login(un, pw) == 0) {
                out_add(tr(S_OK));
                /* load that user's commands */
                char upath[32]; user_cmd_path(upath, current_user, 32);
                ucmd_init();
                ucmd_load(upath);
            } else out_add("ERR: bad user/pass");
        } else if (p[0] == 'N' && p[1] == 'E' && p[2] == 'W') {
            p = next_arg(p + 3);
            char un[USER_NAME_SZ]; p = copy_arg(un, p, USER_NAME_SZ);
            char pw[USER_PASS_SZ]; copy_arg(pw, p, USER_PASS_SZ);
            if (users_new(un, pw) == 0) out_add(tr(S_CREATED));
            else out_add("ERR: max users or exists");
        } else if (p[0] == 'L' && p[1] == 'I' && p[2] == 'S' && p[3] == 'T') {
            if (users_count == 0) { out_add("No users"); return; }
            for (int i = 0; i < users_count; i++) out_add(users_name[i]);
        } else if (p[0] == 'W' && p[1] == 'H' && p[2] == 'O') {
            if (current_user[0]) out_add(current_user);
            else out_add("(none)");
        } else if (p[0] == 'L' && p[1] == 'O' && p[2] == 'G' && p[3] == 'O' && p[4] == 'U' && p[5] == 'T') {
            users_logout();
            out_add(tr(S_OK));
        } else {
            out_add("USER: LOGIN name pass | NEW name pass | LIST | WHO | LOGOUT");
        }
    } else if (buf[0] == 'L' && buf[1] == 'A' && buf[2] == 'N' && buf[3] == 'G') {
        int nl = LANG_EN;
        if (buf[4] == ' ') {
            char lc = buf[5];
            if (lc == 'E') nl = LANG_EN;
            else if (lc == 'S' || lc == 's') nl = LANG_ES;
            else if (lc == 'F' || lc == 'f') nl = LANG_FR;
            else if (lc == 'D' || lc == 'd') nl = LANG_DE;
            else if (lc == 'P' || lc == 'p') nl = LANG_PT;
        }
        lang_set((lang_t)nl);
        out_add(tr(S_OK));
    } else if (buf[0]) {
        /* check user-defined commands */
        int found = 0;
        char first[UCMD_NAME];
        int fi = 0;
        while (buf[fi] && buf[fi] != ' ' && fi < UCMD_NAME - 1) { first[fi] = buf[fi]; fi++; }
        first[fi] = 0;
        for (int ui = 0; ui < ucmd_count; ui++) {
            if (strcmp(first, ucmd_name[ui]) == 0) {
                /* build effective command: action + remaining args */
                char combined[64];
                int ci = 0;
                for (int j = 0; ucmd_act[ui][j] && ci < 63; j++)
                    combined[ci++] = ucmd_act[ui][j];
                /* skip past command name in original buf */
                int bi = fi;
                while (buf[bi] == ' ') bi++;
                if (buf[bi]) {
                    if (ci < 63) combined[ci++] = ' ';
                    while (buf[bi] && ci < 63) combined[ci++] = buf[bi++];
                }
                combined[ci] = 0;
                eval_cmd(combined);
                found = 1;
                break;
            }
        }
        if (!found) out_add(tr(S_UNKNOWN));
    }
    eval_depth--;
}

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

        eval_cmd(shell_buf);

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
