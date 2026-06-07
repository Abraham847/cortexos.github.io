#include "kernel.h"
#include "bios.h"
#include "fs.h"
#include "nn.h"
#include "nn_float.h"
#include "ipc.h"
#include "model.h"
#include "lang.h"
#include "heap.h"
#include "kernel_api.h"
#include "loader.h"
#include "sysmon.h"
#include "fman.h"
#include "edit.h"
#include "forth.h"

static char shell_buf[80];
static int shell_pos;
#define OUT_LINES 64
#define OUT_COLS 64
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
            int d = *s - '0';
            if (phase == 0) { if (a > 214748364) a = 2147483647; else a = a * 10 + d; }
            else { if (b > 214748364) b = 2147483647; else b = b * 10 + d; }
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
    while (*s >= '0' && *s <= '9') {
        int d = *s++ - '0';
        if (v > 214748364) { v = 2147483647; while (*s >= '0' && *s <= '9') s++; break; }
        v = v * 10 + d;
    }
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

/* ---- command table ---- */
typedef struct { const char *name; void (*fn)(const char *arg); } cmd_t;

static void cmd_help(const char *a) {
    (void)a;
    out_add("HELP   - this text");       out_add("CALC   - a+b / a-b / a*b");
    out_add("INFO   - system info");      out_add("CLEAR  - clear output");
    out_add("DIR    - list files");       out_add("DEL x  - delete file");
    out_add("REN a b- rename file");      out_add("EDIT x - text editor");
    out_add("ECHO x - print text");       out_add("FORTH  - FORTH repl");
    out_add("PAINT  - drawing app");      out_add("NEURAL - open NN monitor");
    out_add("NNEDIT - open NN editor");   out_add("DSVIEW - open dataset view");
    out_add("WEIGHTS- open weight view"); out_add("NN ... - NN commands");
    out_add("DS ... - dataset mgr");      out_add("MSG    - IPC: SEND/RECV");
    out_add("MODEL  - model mgr");        out_add("CMD    - user cmd mgr");
    out_add("USER   - account mgr");      out_add("PS     - list tasks");
    out_add("LANG   - set language (EN/ES/FR/DE/PT)");
    out_add("SYSMON - system monitor");   out_add("FMAN   - file manager");
    out_add("NET    - net info/DHCP/test/scan/NS/TCP");
    out_add("PING   - ICMP echo to IP");
    out_add("DATE   - show RTC date/time");
    out_add("MEM    - memory stats");    out_add("BEEP   - PC speaker beep");
    out_add("RUN    - run app.bin");      out_add("EXEC   - run batch.txt");
    out_add("SOUND  - PLAY/vol (SB16)");
    out_add("CAT x  - show file contents");
}
static void cmd_calc(const char *a) { out_int(eval_expr(a)); }
static void cmd_echo(const char *a) { out_add(*a ? a : ""); }
static void cmd_info(const char *a) {
    (void)a; char db[16];
    out_add("CortexOS v1.0"); out_add("CPU: i686 32bit PMode");
    itoa(vga_width, db); out_add("Display: "); out_add(db); out_add("x");
    itoa(vga_height, db); out_add(db);
    u32 mem = bios_total_mem_kb(); itoa(mem/1024, db);
    out_add("RAM: "); out_add(db); out_add("MB");
    itoa(timer_ticks/100, db); out_add("Up: "); out_add(db); out_add("s");
}
static void cmd_clear(const char *a) { (void)a; out_lines = 0; }
static void cmd_sysmon(const char *a) { (void)a; extern void sysmon_open(void); sysmon_open(); out_add(tr(S_OK)); }
static void cmd_fman(const char *a) { (void)a; extern void fman_open(void); fman_open(); out_add(tr(S_OK)); }
static void cmd_dir(const char *a) {
    (void)a; fs_entry_t ents[50];
    int n = fs_list("", ents, 50);
    if (n > 0) {
        char dbuf[32];
        for (int i = 0; i < n && i < 14; i++) {
            int j; for (j = 0; ents[i].name[j]; j++) dbuf[j] = ents[i].name[j];
            dbuf[j]=' '; j++; for (;j<20;j++) dbuf[j]=' ';
            itoa(ents[i].size, dbuf+12); out_add(dbuf);
        }
    } else out_add(n==0 ? tr(S_EMPTY) : tr(S_ERR));
}
static void cmd_del(const char *a) { out_add(fs_delete(a)==0 ? tr(S_OK) : tr(S_ERR)); }
static void cmd_ren(const char *a) {
    char old[20],new[20]; int oi=0,ni=0;
    while (*a==' ') a++; while (*a&&*a!=' '&&oi<19) old[oi++]=*a++; old[oi]=0;
    while (*a==' ') a++; while (*a&&ni<19) new[ni++]=*a++; new[ni]=0;
    out_add(oi&&ni&&fs_rename(old,new)==0 ? tr(S_OK) : tr(S_ERR));
}
static void cmd_neural(const char *a) { (void)a; ai_open_trainer(0); out_add(tr(S_OK)); }
static void cmd_nnedit(const char *a) { (void)a; ai_open_editor(0); out_add(tr(S_OK)); }
static void cmd_dsview(const char *a) { (void)a; ai_open_ds_view(); out_add(tr(S_OK)); }
static void cmd_weights(const char *a) {
    int si = *a ? atoi_s(a) : 0;
    if (si<0||si>=ai_get_slot_count()||!ai_slot_ready(si)) out_add(tr(S_NOSLOT));
    else { ai_open_weights(si); out_add(tr(S_OK)); }
}
static void cmd_edit(const char *a) { edit_open(*a?a:0); out_add(tr(S_EDITOR)); out_add(" "); out_add(tr(S_OK)); }
static void cmd_forth(const char *a) { (void)a; forth_open(); out_add(tr(S_FORTH)); out_add(" "); out_add(tr(S_OK)); }
static void cmd_run(const char *a) {
    if (!*a) { out_add("Usage: RUN filename.BIN [args]"); return; }
    char fname[32]; int fi = 0;
    while (a[fi] && a[fi] != ' ' && fi < 30) { fname[fi] = a[fi]; fi++; }
    fname[fi] = 0;
    if (app_load(fname, &kernel_api) == 0)
        out_add(tr(S_OK));
    else
        out_add("ERR: load failed");
}

static void cmd_paint(const char *a) {
    (void)a;
    if (app_load("PAINT.BIN",&kernel_api)==0) { out_add(tr(S_PAINT)); out_add(" "); out_add(tr(S_OK)); }
    else { paint_open(); out_add(tr(S_PAINT)); out_add(" "); out_add(tr(S_OK)); }
}
static void cmd_nnview(const char *a) {
    (void)a;
    nnview_open();
    out_add("NNView");
    out_add(" ");
    out_add(tr(S_OK));
}
static void cmd_ps(const char *a) {
    (void)a; char pbuf[16];
    itoa(task_count(),pbuf); out_add(tr(S_PS)); out_add(pbuf);
    itoa(ai_get_slot_count(),pbuf); out_add("NN:"); out_add(pbuf);
    for (int si=0;si<ai_get_slot_count();si++) if (ai_slot_ready(si)) {
        char sb[24]; int sp=0; itoa(si,sb); sp=strlen(sb);
        sb[sp++]=':'; sb[sp++]=' '; sb[sp]=0; out_add(sb);
    }
}
static void cmd_lang(const char *a) {
    int nl=LANG_EN;
    if (*a) { char lc=*a; if (lc=='E') nl=LANG_EN; else if (lc=='S'||lc=='s') nl=LANG_ES;
             else if (lc=='F'||lc=='f') nl=LANG_FR; else if (lc=='D'||lc=='d') nl=LANG_DE;
             else if (lc=='P'||lc=='p') nl=LANG_PT; }
    lang_set((lang_t)nl); out_add(tr(S_OK));
}

static nn_float_t nnf_net;
static int nnf_ready;
static float *nnf_ds_in, *nnf_ds_out;
static int nnf_ds_n, nnf_ds_ni, nnf_ds_no;

static void nnf_ds_free(void) {
    kfree(nnf_ds_in); kfree(nnf_ds_out);
    nnf_ds_in = 0; nnf_ds_out = 0;
    nnf_ds_n = 0;
}

static void nnf_add_float_str(char *buf, int *pos, int max, float v) {
    char tmp[16];
    int neg = 0;
    if (v < 0) { neg = 1; v = -v; }
    int ip = (int)v;
    int fp = (int)((v - ip) * 100 + 0.5f);
    if (fp >= 100) { ip++; fp = 0; }
    itoa(ip, tmp);
    int k = 0; while (tmp[k] && *pos < max - 2) buf[(*pos)++] = tmp[k++];
    if (*pos < max - 3) buf[(*pos)++] = '.';
    if (fp < 10 && *pos < max - 2) buf[(*pos)++] = '0';
    itoa(fp, tmp);
    k = 0; while (tmp[k] && *pos < max - 1) buf[(*pos)++] = tmp[k++];
}

static void cmd_nnf(const char *a) {
    if (!*a) { out_add("NNF: CREATE TRAIN INFER SAVE LOAD LOADDS GEN DEMO INFO"); return; }
    char *p = (char*)a;
    if (p[0]=='C'&&p[1]=='R'&&p[2]=='E'&&p[3]=='A'&&p[4]=='T'&&p[5]=='E') {
        p = next_arg(p+6); int sz[NNF_MAX_L], nl = 0;
        while (*p && nl < NNF_MAX_L) { sz[nl++] = atoi_s(p); p = next_arg(p); }
        if (nl >= 2) {
            if (nnf_ready) nnf_free(&nnf_net);
            if (nnf_init(&nnf_net, nl, sz) == 0) {
                nnf_rand(&nnf_net, 2.0f, 0);
                nnf_ready = 1;
                out_add("NNF created");
            } else out_add(tr(S_ERR));
        } else out_add("ERR: need 2+ layers");
    } else if (p[0]=='T'&&p[1]=='R'&&p[2]=='A'&&p[3]=='I'&&p[4]=='N') {
        p = next_arg(p+5); int ep = *p ? atoi_s(p) : 10; p = next_arg(p);
        int lrint = *p ? atoi_s(p) : 50;
        float lr = lrint / 100.0f;
        if (!nnf_ready) { out_add("ERR: no net"); return; }
        if (nnf_ds_n == 0) { out_add("ERR: no dataset. Use NNF LOADDS or NNF GEN"); return; }
        for (int e = 0; e < ep; e++) {
            float loss = 0;
            for (int s = 0; s < nnf_ds_n; s++) {
                float *in = nnf_ds_in + s * nnf_ds_ni;
                float *out = nnf_ds_out + s * nnf_ds_no;
                nnf_fwd(&nnf_net, in);
                loss += nnf_bwd(&nnf_net, out, lr);
            }
            if ((e + 1) % 10 == 0 || e == 0 || e == ep - 1) {
                char sb[24]; int pos = 0;
                char tmp[16];
                itoa(e + 1, tmp); int k = 0; while (tmp[k] && pos < 22) sb[pos++] = tmp[k++];
                if (pos < 22) { sb[pos++] = ':'; sb[pos++] = ' '; }
                loss /= nnf_ds_n;
                nnf_add_float_str(sb, &pos, 24, loss);
                if (pos < 24) sb[pos] = 0;
                out_add(sb);
            }
        }
        out_add(tr(S_TRAINED));
    } else if (p[0]=='I'&&p[1]=='N'&&p[2]=='F'&&p[3]=='E'&&p[4]=='R') {
        p = next_arg(p+5);
        if (*p && nnf_ready) {
            float in[16]; int ni = 0;
            while (*p && ni < 16) { in[ni++] = atoi_s(p) / 100.0f; p = next_arg(p); }
            float out[16];
            nnf_infer(&nnf_net, in, out);
            char sb[64]; int pos = 0;
            for (int i = 0; i < nnf_net.sz[nnf_net.nl - 1] && i < 4; i++) {
                char tmp[8]; itoa((int)(out[i] * 100), tmp);
                int k = 0; while (tmp[k] && pos < 60) sb[pos++] = tmp[k++];
                if (pos < 60) sb[pos++] = ' ';
            }
            if (pos < 60) sb[pos] = 0;
            out_add(sb);
        } else out_add("ERR");
    } else if (p[0]=='S'&&p[1]=='A'&&p[2]=='V'&&p[3]=='E') {
        p = next_arg(p+4); const char *fn = *p ? p : "NNFMOD.BIN";
        if (!nnf_ready) { out_add("ERR: no net"); return; }
        out_add(nnf_save(&nnf_net, fn) == 0 ? tr(S_SAVED) : tr(S_ERR));
    } else if (p[0]=='L'&&p[1]=='O'&&p[2]=='A'&&p[3]=='D') {
        p = next_arg(p+4); const char *fn = *p ? p : "NNFMOD.BIN";
        if (nnf_ready) nnf_free(&nnf_net);
        out_add(nnf_load(&nnf_net, fn) == 0 ? (nnf_ready = 1, tr(S_LOADED)) : tr(S_ERR));
    } else if (p[0]=='L'&&p[1]=='O'&&p[2]=='A'&&p[3]=='D'&&p[4]=='D'&&p[5]=='S') {
        p = next_arg(p+6);
        if (*p) {
            nnf_ds_free();
            dataset tmp;
            if (ds_load(&tmp, p) == 0) {
                nnf_ds_n = tmp.n; nnf_ds_ni = tmp.ni; nnf_ds_no = tmp.no;
                nnf_ds_in = (float*)kmalloc(nnf_ds_n * nnf_ds_ni * sizeof(float));
                nnf_ds_out = (float*)kmalloc(nnf_ds_n * nnf_ds_no * sizeof(float));
                if (nnf_ds_in && nnf_ds_out) {
                    for (int i = 0; i < nnf_ds_n * nnf_ds_ni; i++)
                        nnf_ds_in[i] = tmp.in[i] / 65536.0f;
                    for (int i = 0; i < nnf_ds_n * nnf_ds_no; i++)
                        nnf_ds_out[i] = tmp.out[i] / 65536.0f;
                    out_add(tr(S_LOADED));
                } else { nnf_ds_free(); out_add(tr(S_ERR)); }
                kfree(tmp.in); kfree(tmp.out);
            } else out_add(tr(S_ERR));
        } else out_add("Usage: NNF LOADDS file.bin");
    } else if (p[0]=='G'&&p[1]=='E'&&p[2]=='N') {
        p = next_arg(p+3);
        if (*p) {
            nnf_ds_free();
            nnf_ds_ni = 2; nnf_ds_no = 1;
            float x[4][2] = {{0,0},{0,1},{1,0},{1,1}};
            float y[4][1];
            if (p[0]=='X'||p[0]=='x') {
                y[0][0]=0; y[1][0]=1; y[2][0]=1; y[3][0]=0;
            } else if (p[0]=='A'||p[0]=='a') {
                y[0][0]=0; y[1][0]=0; y[2][0]=0; y[3][0]=1;
            } else if (p[0]=='O'||p[0]=='o') {
                y[0][0]=0; y[1][0]=1; y[2][0]=1; y[3][0]=1;
            } else { out_add("USAGE: NNF GEN XOR|AND|OR"); return; }
            nnf_ds_n = 4;
            nnf_ds_in = (float*)kmalloc(4 * 2 * sizeof(float));
            nnf_ds_out = (float*)kmalloc(4 * 1 * sizeof(float));
            if (nnf_ds_in && nnf_ds_out) {
                for (int i = 0; i < 4; i++) {
                    nnf_ds_in[i*2] = x[i][0]; nnf_ds_in[i*2+1] = x[i][1];
                    nnf_ds_out[i] = y[i][0];
                }
                char sb[16]; itoa(nnf_ds_n, sb);
                out_add("Generated "); out_add(sb); out_add(" samples");
            } else nnf_ds_free();
        } else out_add("USAGE: NNF GEN XOR|AND|OR");
    } else if (p[0]=='D'&&p[1]=='E'&&p[2]=='M'&&p[3]=='O') {
        nnf_ds_free();
        nnf_ds_ni = 2; nnf_ds_no = 1; nnf_ds_n = 4;
        nnf_ds_in = (float*)kmalloc(4 * 2 * sizeof(float));
        nnf_ds_out = (float*)kmalloc(4 * 1 * sizeof(float));
        if (!nnf_ds_in || !nnf_ds_out) { nnf_ds_free(); out_add("ERR: no mem"); return; }
        for (int i = 0; i < 4; i++) nnf_ds_out[i] = 0;
        nnf_ds_in[0]=0; nnf_ds_in[1]=0; nnf_ds_out[0]=0;
        nnf_ds_in[2]=0; nnf_ds_in[3]=1; nnf_ds_out[1]=1;
        nnf_ds_in[4]=1; nnf_ds_in[5]=0; nnf_ds_out[2]=1;
        nnf_ds_in[6]=1; nnf_ds_in[7]=1; nnf_ds_out[3]=0;
        if (nnf_ready) nnf_free(&nnf_net);
        int sz[] = {2, 4, 1};
        if (nnf_init(&nnf_net, 3, sz)) { out_add("ERR: init"); return; }
        nnf_rand(&nnf_net, 1.0f, 123);
        nnf_ready = 1;
        out_add("XOR demo: training...");
        for (int e = 0; e < 200; e++) {
            float loss = 0;
            for (int s = 0; s < 4; s++) {
                nnf_fwd(&nnf_net, nnf_ds_in + s * 2);
                loss += nnf_bwd(&nnf_net, nnf_ds_out + s, 0.5f);
            }
            if ((e + 1) % 50 == 0) {
                char sb[24]; int pos = 0;
                char tmp[16]; itoa(e + 1, tmp);
                int k = 0; while (tmp[k] && pos < 22) sb[pos++] = tmp[k++];
                if (pos < 22) { sb[pos++] = ':'; sb[pos++] = ' '; }
                nnf_add_float_str(sb, &pos, 24, loss / 4.0f);
                if (pos < 24) sb[pos] = 0;
                out_add(sb);
            }
        }
        nnf_save(&nnf_net, "XOR.BIN");
        out_add("Model saved to XOR.BIN");
        out_add("Results:");
        float test_in[4][2] = {{0,0},{0,1},{1,0},{1,1}};
        float out;
        for (int t = 0; t < 4; t++) {
            nnf_infer(&nnf_net, test_in[t], &out);
            char sb[32]; int pos = 0;
            nnf_add_float_str(sb, &pos, 32, test_in[t][0]);
            if (pos < 30) sb[pos++] = ' ';
            nnf_add_float_str(sb, &pos, 32, test_in[t][1]);
            if (pos < 30) { sb[pos++] = ' '; sb[pos++] = '-'; sb[pos++] = '>'; sb[pos++] = ' '; }
            nnf_add_float_str(sb, &pos, 32, out);
            if (pos < 32) sb[pos] = 0;
            out_add(sb);
        }
    } else if (p[0]=='I'&&p[1]=='N'&&p[2]=='F'&&p[3]=='O') {
        if (!nnf_ready) { out_add("ERR: no net"); return; }
        char sb[48]; int pos = 0;
        itoa(nnf_net.nl, sb);
        int i; for (i = 0; sb[i]; i++) sb[pos++] = sb[i];
        for (int l = 0; l < nnf_net.nl; l++) {
            if (pos < 44) sb[pos++] = ' ';
            itoa(nnf_net.sz[l], sb);
            for (i = 0; sb[i] && pos < 44; i++) sb[pos++] = sb[i];
        }
        if (pos < 44) sb[pos] = 0;
        out_add(sb);
    } else out_add("NNF: CREATE TRAIN INFER SAVE LOAD LOADDS INFO");
}

static void cmd_nn(const char *a) {
    if (!*a) { out_add("NN: CREATE TRAIN LOAD SAVE LOADMOD EXPORT INFER ACT AUTO INFO SLOT SAVES LOADS"); return; }
    char *p=(char*)a;
    if (p[0]=='C'&&p[1]=='R'&&p[2]=='E'&&p[3]=='A'&&p[4]=='T'&&p[5]=='E') {
        p=next_arg(p+6); int sz[NN_MAX_L],nl=0;
        while (*p&&nl<NN_MAX_L){sz[nl++]=atoi_s(p);p=next_arg(p);}
        if (nl>=2){ai_create_net(nl,sz);out_add("NN ");out_add(tr(S_CREATED));}
        else out_add("ERR: need 2+ layers");
    } else if (p[0]=='T'&&p[1]=='R'&&p[2]=='A'&&p[3]=='I'&&p[4]=='N') {
        p=next_arg(p+5); int ep=*p?atoi_s(p):100; p=next_arg(p); int lr=*p?atoi_s(p):50;
        int r=ai_train_net(ep,lr);
        if (r==0) out_add(tr(S_TRAINED)); else if (r==-2) out_add("ERR: no dataset"); else out_add(tr(S_ERR));
    } else if (p[0]=='L'&&p[1]=='O'&&p[2]=='A'&&p[3]=='D') {
        p=next_arg(p+4);
        if (*p){if(ai_load_dataset(p)==0){out_add(tr(S_DS));out_add(" ");out_add(tr(S_LOADED));}else out_add(tr(S_ERR));}
        else out_add("Usage: NN LOAD file");
    } else if (p[0]=='S'&&p[1]=='A'&&p[2]=='V'&&p[3]=='E') {
        p=next_arg(p+4); const char *fn=*p?p:"NNMOD.BIN";
        if (ai_save_model(fn)==0){out_add(tr(S_MODEL));out_add(" ");out_add(tr(S_SAVED));}else out_add(tr(S_ERR));
    } else if (p[0]=='L'&&p[1]=='O'&&p[2]=='A'&&p[3]=='D'&&p[4]=='M'&&p[5]=='O'&&p[6]=='D') {
        p=next_arg(p+7); const char *fn=*p?p:"NNMOD.BIN";
        if (ai_load_model(fn)==0){out_add(tr(S_MODEL));out_add(" ");out_add(tr(S_LOADED));}else out_add(tr(S_ERR));
    } else if (p[0]=='E'&&p[1]=='X'&&p[2]=='P'&&p[3]=='O'&&p[4]=='R'&&p[5]=='T') {
        p=next_arg(p+6); const char *fn=*p?p:"NNEXPORT.TXT";
        if (ai_export_txt(fn)==0) out_add(tr(S_EXPORTED)); else out_add(tr(S_ERR));
    } else if (p[0]=='I'&&p[1]=='N'&&p[2]=='F'&&p[3]=='E'&&p[4]=='R') {
        p=next_arg(p+5); if(*p){char res[64];int n=ai_infer_str(p,res,64);if(n>0)out_add(res);else out_add("ERR");}
        else out_add("Usage: NN INFER v1 v2 ...");
    } else if (p[0]=='A'&&p[1]=='C'&&p[2]=='T') {
        p=next_arg(p+3); int layer=atoi_s(p); p=next_arg(p); int act=atoi_s(p);
        ai_set_act(layer,act); out_add(tr(S_ACT_SET));
    } else if (p[0]=='A'&&p[1]=='U'&&p[2]=='T'&&p[3]=='O') {
        p=next_arg(p+4); int on=(*p=='1'||*p=='o'||*p=='O')?1:0;
        ai_set_auto(on); out_add(on?tr(S_AUTO_ON):tr(S_AUTO_OFF));
    } else if (p[0]=='S'&&p[1]=='L'&&p[2]=='O'&&p[3]=='T') {
        p=next_arg(p+4); int si=atoi_s(p);
        if(ai_select_slot(si)==0) out_add(tr(S_SLOT_SEL)); else out_add(tr(S_ERR));
    } else if (p[0]=='S'&&p[1]=='A'&&p[2]=='V'&&p[3]=='E'&&p[4]=='S') {
        p=next_arg(p+5); int si=atoi_s(p); p=next_arg(p);
        if(ai_save_model_slot(si,*p?p:"NNMOD.BIN")==0)out_add(tr(S_SAVED));else out_add(tr(S_ERR));
    } else if (p[0]=='L'&&p[1]=='O'&&p[2]=='A'&&p[3]=='D'&&p[4]=='S') {
        p=next_arg(p+5); int si=atoi_s(p); p=next_arg(p);
        if(ai_load_model_slot(si,*p?p:"NNMOD.BIN")==0)out_add(tr(S_LOADED));else out_add(tr(S_ERR));
    } else if (p[0]=='I'&&p[1]=='N'&&p[2]=='F'&&p[3]=='O') {
        char ibuf[80]; ai_get_info(ibuf,80); out_add(ibuf);
    } else out_add("NN: CREATE TRAIN LOAD SAVE LOADMOD EXPORT INFER ACT AUTO INFO SLOT SAVES LOADS");
}
static void cmd_ds(const char *a) {
    if (!*a) { out_add("DS: CREATE ADD SAVE EXPORT IMPORT CLEAR STAT"); return; }
    char *p=(char*)a;
    if (p[0]=='C'&&p[1]=='R'&&p[2]=='E'&&p[3]=='A'&&p[4]=='T'&&p[5]=='E') {
        p=next_arg(p+6); int ni=atoi_s(p); p=next_arg(p); int no=atoi_s(p);
        out_add(ni>0&&no>0&&ds_create_mgr(ni,no)==0?tr(S_DS_READY):tr(S_ERR));
    } else if (p[0]=='A'&&p[1]=='D'&&p[2]=='D') {
        p=next_arg(p+3); if(*p){int r=ds_add_sample(p);if(r==0){char cbuf[16];itoa(ds_mgr_count(),cbuf);out_add(tr(S_SAMPLE_ADD));out_add(cbuf);}else if(r==-2)out_add("ERR: wrong dim");else out_add(tr(S_ERR));}
        else out_add("Usage: DS ADD v1 v2 .. o1 o2 ..");
    } else if (p[0]=='S'&&p[1]=='A'&&p[2]=='V'&&p[3]=='E') {
        p=next_arg(p+4); if(*p){if(ds_save_mgr(p)==0){out_add(tr(S_DS_SAVED));ds_clear_mgr();}else out_add(tr(S_ERR));}
        else out_add("Usage: DS SAVE file.bin");
    } else if (p[0]=='C'&&p[1]=='L'&&p[2]=='E'&&p[3]=='A'&&p[4]=='R') { ds_clear_mgr(); out_add(tr(S_DS_CLR)); }
    else if (p[0]=='E'&&p[1]=='X'&&p[2]=='P'&&p[3]=='O'&&p[4]=='R'&&p[5]=='T') {
        p=next_arg(p+6); if(*p){if(ai_export_text_ds(p)==0)out_add(tr(S_EXPORTED));else out_add(tr(S_ERR));}
        else out_add("Usage: DS EXPORT path.txt");
    } else if (p[0]=='I'&&p[1]=='M'&&p[2]=='P'&&p[3]=='O'&&p[4]=='R'&&p[5]=='T') {
        p=next_arg(p+6); char dspath[32];p=copy_arg(dspath,p,20); int ni2=atoi_s(p);p=next_arg(p); int no2=atoi_s(p);
        if(ni2>0&&no2>0){if(ai_import_text_ds(dspath,ni2,no2)==0)out_add(tr(S_LOADED));else out_add(tr(S_ERR));}
        else out_add("Usage: DS IMPORT path.txt ni no");
    } else if (p[0]=='S'&&p[1]=='T'&&p[2]=='A'&&p[3]=='T') {
        int n=ds_mgr_count(); char sbuf[16]; itoa(n,sbuf);
        out_add(tr(S_DS)); out_add(" "); out_add(tr(S_SAMPLES)); out_add(":"); out_add(sbuf);
    } else if (p[0]=='G'&&p[1]=='E'&&p[2]=='N') {
        p=next_arg(p+3); if(*p){if(ds_generate(p)==0){char sb2[16];itoa(ds_mgr_count(),sb2);out_add("Generated ");out_add(sb2);out_add(" samples");}else out_add("USAGE: DS GEN XOR|AND|OR");}
        else out_add("USAGE: DS GEN XOR|AND|OR");
    } else out_add("DS: CREATE ADD SAVE EXPORT IMPORT CLEAR STAT GEN");
}
static void cmd_msg(const char *a) {
    if (!*a) { out_add("MSG: SEND dst text | RECV | CHECK"); return; }
    char *p=(char*)a;
    if (p[0]=='S'&&p[1]=='E'&&p[2]=='N'&&p[3]=='D') {
        p=next_arg(p+4); int dst=atoi_s(p); p=next_arg(p);
        if(dst>=0&&*p){if(ipc_send(dst,1,p,strlen(p))==0)out_add(tr(S_SENT));else out_add("ERR: full or bad dst");}
        else out_add("Usage: MSG SEND dst text");
    } else if (p[0]=='R'&&p[1]=='E'&&p[2]=='C'&&p[3]=='V') {
        char rbuf[IPC_DATA_SZ+1]; int rtype,rlen; int src=ipc_recv(-1,&rtype,rbuf,&rlen);
        if(src>=0){rbuf[rlen]=0;char it[8];itoa(src,it);out_add(it);out_add(": ");out_add(rbuf);}
        else out_add(tr(S_NOMSG));
    } else if (p[0]=='C'&&p[1]=='H'&&p[2]=='E'&&p[3]=='C'&&p[4]=='K') {
        int n=ipc_available(-1); char it[8];itoa(n,it);out_add(tr(S_PENDING));out_add(it);
    } else out_add("MSG: SEND dst text | RECV | CHECK");
}
static void cmd_model(const char *a) {
    if (!*a) { out_add("MODEL: REG LOAD GET PUT UNREG LIST"); return; }
    char *p=(char*)a;
    if (p[0]=='R'&&p[1]=='E'&&p[2]=='G') {
        p=next_arg(p+3); char mname[24];p=copy_arg(mname,p,16);
        if(*p){nn*net=(nn*)kmalloc(sizeof(nn));if(!net)return;int sz[4],nl=0;while(*p&&nl<4){sz[nl++]=atoi_s(p);p=next_arg(p);}
        if(nl>=2){if(nn_init(net,nl,sz)){kfree(net);out_add(tr(S_ERR));return;}nn_rand(net,FF(2));
        if(model_register(mname,net)==0)out_add(tr(S_OK));else{nn_free(net);kfree(net);out_add(tr(S_ERR));}}
        else{kfree(net);out_add("Usage: MODEL REG name n1 n2 ...");}}else out_add("Usage: MODEL REG name n1 n2 ...");
    } else if (p[0]=='L'&&p[1]=='O'&&p[2]=='A'&&p[3]=='D') {
        p=next_arg(p+4); char mname[24];p=copy_arg(mname,p,16); char mpath[24];p=copy_arg(mpath,p,20);
        if(model_load_file(*mname?mname:"model",*mpath?mpath:"NNMOD.BIN")==0)out_add(tr(S_LOADED));else out_add(tr(S_ERR));
    } else if (p[0]=='G'&&p[1]=='E'&&p[2]=='T') {
        p=next_arg(p+3); char mname[24];copy_arg(mname,p,16); nn*n=model_get(mname);
        if(n){char ob[64];ai_get_info(ob,64);out_add(ob);model_put(mname);}else out_add("ERR");
    } else if (p[0]=='P'&&p[1]=='U'&&p[2]=='T') { p=next_arg(p+3); out_add(model_put(p)>=0?tr(S_RELEASED):tr(S_ERR)); }
    else if (p[0]=='U'&&p[1]=='N'&&p[2]=='R'&&p[3]=='E'&&p[4]=='G') {
        p=next_arg(p+5); out_add(model_unregister(p)==0?tr(S_UNREG):"ERR: has refs or not found");
    } else if (p[0]=='L'&&p[1]=='I'&&p[2]=='S'&&p[3]=='T') {
        char lb[128];model_list(lb,128);out_add(lb);char it[16];itoa(model_count(),it);
        out_add(tr(S_MODEL));out_add(":");out_add(it);
    } else out_add("MODEL: REG LOAD GET PUT UNREG LIST");
}
static void cmd_cmd(const char *a) {
    if (!*a) { out_add("CMD: UNLOCK <pw> | LOCK | NEW name action | DEL name | LIST | SAVE [file] | LOAD [file] | USERS [file]"); return; }
    char *p=(char*)a;
    if (p[0]=='U'&&p[1]=='N'&&p[2]=='L'&&p[3]=='O'&&p[4]=='C'&&p[5]=='K') {
        p=next_arg(p+6); out_add(cmd_unlock(p)==0?tr(S_OK):"ERR: wrong password");
    } else if (p[0]=='L'&&p[1]=='O'&&p[2]=='C'&&p[3]=='K') { cmd_lock(); out_add(tr(S_OK)); }
    else if (cmd_locked) { out_add("Locked. Use: CMD UNLOCK <password>"); }
    else if (p[0]=='N'&&p[1]=='E'&&p[2]=='W') {
        p=next_arg(p+3); char cn[UCMD_NAME];p=copy_arg(cn,p,UCMD_NAME);
        if(*p){if(ucmd_add(cn,p)==0){out_add("CMD ");out_add(cn);out_add(" ");out_add(tr(S_CREATED));}else out_add("ERR: max commands reached");}
        else out_add("Usage: CMD NEW name action");
    } else if (p[0]=='D'&&p[1]=='E'&&p[2]=='L') { p=next_arg(p+3); out_add(ucmd_del(p)==0?tr(S_OK):"ERR: not found"); }
    else if (p[0]=='L'&&p[1]=='I'&&p[2]=='S'&&p[3]=='T') {
        if(ucmd_count==0){out_add("No user commands");return;}
        for(int i=0;i<ucmd_count;i++){char lb[UCMD_NAME+UCMD_ACT+4];int j;for(j=0;ucmd_name[i][j];j++)lb[j]=ucmd_name[i][j];for(;j<16;j++)lb[j]=' ';lb[16]='=';lb[17]=' ';int k;for(k=0;ucmd_act[i][k];k++)lb[18+k]=ucmd_act[i][k];lb[18+k]=0;out_add(lb);}
    } else if (p[0]=='S'&&p[1]=='A'&&p[2]=='V'&&p[3]=='E') {
        p=next_arg(p+4); if(*p){ucmd_save(p);out_add(tr(S_SAVED));}else if(current_user[0]){char upath[32];user_cmd_path(upath,current_user,32);ucmd_save(upath);out_add(tr(S_SAVED));}else out_add("Usage: CMD SAVE [path]");
    } else if (p[0]=='L'&&p[1]=='O'&&p[2]=='A'&&p[3]=='D') {
        p=next_arg(p+4); const char *loadpath="CMDS.TXT";char upath[32];if(*p)loadpath=p;else if(current_user[0]){user_cmd_path(upath,current_user,32);loadpath=upath;}
        out_add(ucmd_load(loadpath)==0?tr(S_LOADED):tr(S_ERR));
    } else if (p[0]=='U'&&p[1]=='S'&&p[2]=='E'&&p[3]=='R'&&p[4]=='S') { p=next_arg(p+5); users_save(*p?p:"USERS.TXT"); out_add(tr(S_SAVED)); }
    else out_add("CMD: UNLOCK <pw> | LOCK | NEW name action | DEL name | LIST | SAVE [file] | LOAD [file] | USERS [file]");
}
static int strneq(const char *a, const char *b, int n) {
    for (int i = 0; i < n; i++) if (a[i] != b[i]) return 0;
    return 1;
}

static int parse_oct(const char **p) {
    int v = 0;
    while (**p >= '0' && **p <= '9') {
        int d = **p - '0'; (*p)++;
        if (v > 214748364) { v = 2147483647; while (**p >= '0' && **p <= '9') (*p)++; break; }
        v = v * 10 + d;
    }
    return v;
}

static void cmd_exec(const char *a) {
    if (!*a) { out_add("Usage: EXEC <file.txt>"); return; }
    char *buf = (char*)kmalloc(2048);
    if (!buf) { out_add("ERR: no mem"); return; }
    int n = fs_read(a, (u8*)buf, 2047);
    if (n <= 0) { kfree(buf); out_add("ERR: not found"); return; }
    buf[n] = 0;
    char *line = buf;
    int lineno = 0;
    while (*line && lineno < 50) {
        while (*line == '\r' || *line == '\n' || *line == ' ') line++;
        if (!*line) break;
        char *end = line;
        while (*end && *end != '\n' && *end != '\r') end++;
        char saved = *end; *end = 0;
        lineno++;
        char sb[8]; itoa(lineno, sb);
        out_add("> "); out_add(sb); out_add(": "); out_add(line);
        eval_cmd(line);
        *end = saved;
        line = end;
        if (*line == '\n') line++;
        if (*line == '\r') line++;
    }
    kfree(buf);
    out_add("Batch done");
}

static void cmd_cat(const char *a) {
    if (*a == 0) { out_add("CAT <file>"); return; }
    u8 buf[512];
    int n = fs_read(a, buf, 511);
    if (n <= 0) { out_add("ERR: not found"); return; }
    buf[n] = 0;
    out_add((const char*)buf);
}

static void cmd_beep(const char *a) {
    (void)a;
    outb(0x61, inb(0x61) | 3);
    outb(0x43, 0xB6);
    u16 div = 1193180 / 440;
    outb(0x42, div & 0xFF);
    outb(0x42, div >> 8);
    for (volatile int i = 0; i < 2000000; i++);
    outb(0x61, inb(0x61) & ~3);
    out_add("Beep!");
}

static void cmd_date(const char *a) {
    (void)a;
    rtc_time_t t;
    rtc_read(&t);
    char db[16];
    itoa(t.year, db); out_add(db); out_add("-");
    itoa(t.mon, db); if (t.mon < 10) { char b[3] = {'0', db[0], 0}; out_add(b); } else out_add(db);
    out_add("-");
    itoa(t.day, db); if (t.day < 10) { char b[3] = {'0', db[0], 0}; out_add(b); } else out_add(db);
    out_add(" ");
    itoa(t.hour, db); if (t.hour < 10) { char b[3] = {'0', db[0], 0}; out_add(b); } else out_add(db);
    out_add(":");
    itoa(t.min, db); if (t.min < 10) { char b[3] = {'0', db[0], 0}; out_add(b); } else out_add(db);
    out_add(":");
    itoa(t.sec, db); if (t.sec < 10) { char b[3] = {'0', db[0], 0}; out_add(b); } else out_add(db);
}

static void cmd_ping(const char *a) {
    if (*a == 0) { out_add("PING <ip>"); return; }
    ip4_t ip;
    const char *p = a;
    ip.addr[0] = parse_oct(&p); if (*p == '.') p++;
    ip.addr[1] = parse_oct(&p); if (*p == '.') p++;
    ip.addr[2] = parse_oct(&p); if (*p == '.') p++;
    ip.addr[3] = parse_oct(&p);
    char db[16];
    itoa(ip.addr[0], db); out_add("Pinging "); out_add(db); out_add(".");
    itoa(ip.addr[1], db); out_add(db); out_add(".");
    itoa(ip.addr[2], db); out_add(db); out_add(".");
    itoa(ip.addr[3], db); out_add(db);
    int r = net_ping(ip);
    if (r < 0) out_add("ARP failed");
    else out_add("Sent 1 pkt");
}

static void cmd_net(const char *a) {
    if (strcmp(a, "INFO") == 0 || *a == 0) {
        u8 mac[6];
        rtl8139_get_mac(mac);
        char buf[32];
        itohex(mac[0], buf); out_add("MAC: "); out_add(buf);
        itohex(mac[1], buf); out_add(":"); out_add(buf);
        itohex(mac[2], buf); out_add(":"); out_add(buf);
        itohex(mac[3], buf); out_add(":"); out_add(buf);
        itohex(mac[4], buf); out_add(":"); out_add(buf);
        itohex(mac[5], buf); out_add(buf);
        out_add(rtl8139_is_link_up() ? "Link: UP" : "Link: DOWN");
        char db[16];
        itoa(net_my_ip.addr[0], db); out_add("IP: "); out_add(db); out_add(".");
        itoa(net_my_ip.addr[1], db); out_add(db); out_add(".");
        itoa(net_my_ip.addr[2], db); out_add(db); out_add(".");
        itoa(net_my_ip.addr[3], db); out_add(db);
        return;
    }
    if (strneq(a, "NS ", 3)) {
        ip4_t dns_ip;
        if (dns_resolve(a + 3, &dns_ip) == 0) {
            char db[16];
            itoa(dns_ip.addr[0], db); out_add(db); out_add(".");
            itoa(dns_ip.addr[1], db); out_add(db); out_add(".");
            itoa(dns_ip.addr[2], db); out_add(db); out_add(".");
            itoa(dns_ip.addr[3], db); out_add(db);
        } else out_add("DNS failed");
        return;
    }
    if (strcmp(a, "DHCP") == 0) {
        out_add("DHCP requesting...");
        if (dhcp_request() == 0) {
            char db[16];
            itoa(net_my_ip.addr[0], db); out_add("Got IP: "); out_add(db); out_add(".");
            itoa(net_my_ip.addr[1], db); out_add(db); out_add(".");
            itoa(net_my_ip.addr[2], db); out_add(db); out_add(".");
            itoa(net_my_ip.addr[3], db); out_add(db);
        } else out_add("DHCP failed");
        return;
    }
    if (strcmp(a, "TEST") == 0) {
        u8 pkt[] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF, 0,0,0,0,0,0, 0x08,0x00, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
        u8 mymac[6]; rtl8139_get_mac(mymac);
        for (int i = 6; i < 12; i++) pkt[i] = mymac[i - 6];
        int n = rtl8139_send(pkt, 60);
        if (n > 0) { char db[16]; itoa(n, db); out_add("Sent "); out_add(db); out_add(" bytes"); }
        else out_add("Send failed");
        return;
    }
    if (strneq(a, "IP ", 3)) { net_set_ip(a + 3); out_add("IP updated"); return; }
    if (strneq(a, "TCP ", 4)) {
        ip4_t tip;
        const char *p2 = a + 4;
        tip.addr[0] = parse_oct(&p2); if (*p2 == '.') p2++;
        tip.addr[1] = parse_oct(&p2); if (*p2 == '.') p2++;
        tip.addr[2] = parse_oct(&p2); if (*p2 == '.') p2++;
        tip.addr[3] = parse_oct(&p2);
        if (*p2 == ':') p2++;
        int tport = parse_oct(&p2);
        char db[16];
        itoa(tip.addr[0], db); out_add("TCP "); out_add(db); out_add(".");
        itoa(tip.addr[1], db); out_add(db); out_add(".");
        itoa(tip.addr[2], db); out_add(db); out_add(".");
        itoa(tip.addr[3], db); out_add(db); out_add(":");
        itoa(tport, db); out_add(db);
        int sd = tcp_connect(tip, tport);
        if (sd < 0) { out_add(" FAILED"); return; }
        out_add(" OK");
        const char *http = "GET / HTTP/1.0\r\nHost: localhost\r\n\r\n";
        tcp_send(sd, (const u8*)http, strlen(http));
        u8 rbuf[1024];
        int rn = tcp_recv(sd, rbuf, 1023);
        if (rn > 0) { rbuf[rn] = 0; out_add((const char*)rbuf); }
        tcp_close(sd);
        out_add("TCP done");
        return;
    }
    if (strcmp(a, "SCAN") == 0) {
        out_add("ARP scanning 10.0.2.0/24...");
        ip4_t target;
        for (int i = 1; i < 20; i++) {
            target.addr[0] = 10; target.addr[1] = 0; target.addr[2] = 2; target.addr[3] = i;
            u8 mac[6];
            if (net_arp_resolve(target, mac)) {
                char db[16];
                itoa(i, db); out_add("Host 10.0.2."); out_add(db); out_add(" alive");
            }
        }
        out_add("Scan done");
        return;
    }
    out_add("NET [INFO|TEST|IP <addr>|SCAN]");
}

static void cmd_user(const char *a) {
    if (!*a) { out_add("USER: LOGIN name pass | NEW name pass | LIST | WHO | LOGOUT"); return; }
    char *p=(char*)a;
    if (p[0]=='L'&&p[1]=='O'&&p[2]=='G'&&p[3]=='I'&&p[4]=='N') {
        p=next_arg(p+5); char un[USER_NAME_SZ];p=copy_arg(un,p,USER_NAME_SZ); char pw[USER_PASS_SZ];copy_arg(pw,p,USER_PASS_SZ);
        if(users_login(un,pw)==0){out_add(tr(S_OK));char upath[32];user_cmd_path(upath,current_user,32);ucmd_init();ucmd_load(upath);}
        else out_add("ERR: bad user/pass");
    } else if (p[0]=='N'&&p[1]=='E'&&p[2]=='W') {
        p=next_arg(p+3); char un[USER_NAME_SZ];p=copy_arg(un,p,USER_NAME_SZ); char pw[USER_PASS_SZ];copy_arg(pw,p,USER_PASS_SZ);
        out_add(users_new(un,pw)==0?tr(S_CREATED):"ERR: max users or exists");
    } else if (p[0]=='L'&&p[1]=='I'&&p[2]=='S'&&p[3]=='T') { if(users_count==0){out_add("No users");return;} for(int i=0;i<users_count;i++)out_add(users_name[i]); }
    else if (p[0]=='W'&&p[1]=='H'&&p[2]=='O') { out_add(current_user[0]?current_user:"(none)"); }
    else if (p[0]=='L'&&p[1]=='O'&&p[2]=='G'&&p[3]=='O'&&p[4]=='U'&&p[5]=='T') { users_logout(); out_add(tr(S_OK)); }
    else out_add("USER: LOGIN name pass | NEW name pass | LIST | WHO | LOGOUT");
}

static void cmd_sound(const char *a) {
    if (a[0]=='P' && a[1]=='L' && a[2]=='A' && a[3]=='Y') {
        int freq = 440, dur = 500;
        const char *p = a + 4;
        while (*p == ' ') p++;
        if (*p >= '0' && *p <= '9') { freq = atoi_s(p); while(*p && *p!=' ')p++; while(*p==' ')p++; }
        if (*p >= '0' && *p <= '9') dur = atoi_s(p);
        static u8 buf[16000];
        int ns = (freq * dur) / 1000;
        if (ns > 15999) ns = 15999;
        for (int i = 0; i < ns; i++) buf[i] = (i % (22050/freq) < (22050/freq)/2) ? 200 : 50;
        int r = sb16_play(buf, ns, 22050);
        char db[16]; itoa(r, db); out_add("play: "); out_add(db); out_add(" samples");
    } else if (a[0]=='V' && a[1]=='O' && a[2]=='L') {
        int v = 128;
        const char *p = a + 3; while(*p==' ')p++;
        if (*p >= '0' && *p <= '9') v = atoi_s(p);
        sb16_set_volume(v);
        out_add("vol set");
    } else out_add("SOUND PLAY [freq] [ms] | VOL [0-255]");
}

static void cmd_mem(const char *a) {
    (void)a; char db[16];
    int used, free;
    heap_stats(&used, &free);
    out_add("Heap:");
    itoa(used, db); out_add(" used: "); out_add(db); out_add("B");
    itoa(free, db); out_add(" free: "); out_add(db); out_add("B");
    u32 total = bios_total_mem_kb() * 1024;
    itoa(total / 1024, db);
    out_add("Total RAM: "); out_add(db); out_add("KB");
    itoa(heap_get_brk() / 1024, db);
    out_add("Heap brk: "); out_add(db); out_add("KB");
}

static const cmd_t cmd_table[] = {
    {"HELP",cmd_help},{"CALC",cmd_calc},{"ECHO",cmd_echo},{"INFO",cmd_info},
    {"CLEAR",cmd_clear},{"SYSMON",cmd_sysmon},{"FMAN",cmd_fman},
    {"DIR",cmd_dir},{"DEL",cmd_del},{"REN",cmd_ren},
    {"NEURAL",cmd_neural},{"NNEDIT",cmd_nnedit},{"DSVIEW",cmd_dsview},{"WEIGHTS",cmd_weights},
    {"EDIT",cmd_edit},{"FORTH",cmd_forth},{"PAINT",cmd_paint},{"NNVIEW",cmd_nnview},
    {"PS",cmd_ps},{"LANG",cmd_lang},{"MSG",cmd_msg},
    {"NN",cmd_nn},{"NNF",cmd_nnf},{"DS",cmd_ds},{"MODEL",cmd_model},
    {"CMD",cmd_cmd},{"USER",cmd_user},{"NET",cmd_net},{"PING",cmd_ping},{"DATE",cmd_date},{"BEEP",cmd_beep},{"CAT",cmd_cat},{"SOUND",cmd_sound},{"MEM",cmd_mem},{"RUN",cmd_run},{"EXEC",cmd_exec},
};

/* evaluate a command string via table lookup */
static void eval_cmd(const char *buf) {
    if (eval_depth >= EVAL_MAX_DEPTH) { out_add("ERR: depth"); eval_depth = 0; return; }
    eval_depth++;
    if (!buf || !*buf) return;

    char first[16]; int fi = 0;
    while (buf[fi] && buf[fi] != ' ' && fi < 15) { first[fi] = buf[fi]; fi++; }
    first[fi] = 0;
    const char *args = buf[fi] == ' ' ? buf + fi + 1 : "";

    for (int i = 0; i < (int)(sizeof(cmd_table)/sizeof(cmd_table[0])); i++) {
        if (strcmp(first, cmd_table[i].name) == 0) {
            cmd_table[i].fn(args);
            eval_depth--;
            return;
        }
    }

    /* check user-defined commands */
    for (int ui = 0; ui < ucmd_count; ui++) {
        if (strcmp(first, ucmd_name[ui]) == 0) {
            char combined[128]; int ci = 0;
            for (int j = 0; ucmd_act[ui][j] && ci < 127; j++) combined[ci++] = ucmd_act[ui][j];
            if (*args) { if (ci < 127) combined[ci++] = ' '; while (*args && ci < 127) combined[ci++] = *args++; }
            combined[ci] = 0;
            eval_cmd(combined);
            eval_depth--;
            return;
        }
    }

    out_add(tr(S_UNKNOWN));
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
