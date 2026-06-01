#include "kernel.h"
#include "lang.h"

#define STK_SZ 64
#define WORD_SZ 32
#define MAX_WORDS 64

static int ds[STK_SZ], ds_sp;
static int rs[STK_SZ], rs_sp;
static char input[64];
static int input_pos;
static int forth_win_id;

typedef void (*wf)(void);
typedef struct { char n[WORD_SZ]; wf code; } entry;

static entry dict[MAX_WORDS];
static int dcnt;
static int forth_bx, forth_by; /* window-relative draw offset */
static int forth_out_y; /* current output line */

static void push(int v) { if (ds_sp < STK_SZ) ds[ds_sp++] = v; }
static int pop(void) { return ds_sp > 0 ? ds[--ds_sp] : 0; }

static int find(const char *name) {
    for (int i = 0; i < dcnt; i++)
        if (strcmp(dict[i].n, name) == 0) return i;
    return -1;
}

static void reg(const char *name, wf fn) {
    if (dcnt >= MAX_WORDS) return;
    int i; for (i = 0; name[i] && i < WORD_SZ - 1; i++) dict[dcnt].n[i] = name[i];
    dict[dcnt].n[i] = 0; dict[dcnt].code = fn; dcnt++;
}

/* built-in words */
static void w_dot(void) { char b[16]; itoa(pop(), b); vga_drawstring(forth_bx + 5, forth_by + 30 + forth_out_y * 9, b, 7, 1); forth_out_y++; }
static void w_drop(void) { pop(); }
static void w_dup(void) { int v = pop(); push(v); push(v); }
static void w_swap(void) { int a = pop(), b = pop(); push(a); push(b); }
static void w_over(void) { int a = pop(), b = pop(); push(b); push(a); push(b); }
static void w_rot(void) { int a = pop(), b = pop(), c = pop(); push(b); push(a); push(c); }
static void w_add(void) { push(pop() + pop()); }
static void w_sub(void) { int a = pop(); push(pop() - a); }
static void w_mul(void) { push(pop() * pop()); }
static void w_div(void) { int a = pop(); if (a) push(pop() / a); else push(0); }
static void w_eq(void) { push(pop() == pop() ? -1 : 0); }
static void w_lt(void) { int a = pop(); push(pop() < a ? -1 : 0); }
static void w_gt(void) { int a = pop(); push(pop() > a ? -1 : 0); }
static void w_dot_s(void) {
    char b[8]; int y = forth_by + 40;
    for (int i = ds_sp - 1; i >= 0; i--) { itoa(ds[i], b); vga_drawstring(forth_bx, y, b, 7, 1); y += 8; }
}
static void w_emit(void) { int c = pop(); if (c > 0 && c < 128) vga_drawchar(forth_bx + 5, forth_by + 30 + forth_out_y * 9, c, 7, 1); forth_out_y++; }
static void w_cr(void) { forth_out_y++; }
static void w_bye(void) { wm_close(forth_win_id); }

static int exec_word(const char *name) {
    int idx = find(name);
    if (idx >= 0) { dict[idx].code(); return 1; }
    int val = 0, neg = 0, i = 0;
    if (name[0] == '-') { neg = 1; i = 1; }
    while (name[i] >= '0' && name[i] <= '9') { val = val * 10 + (name[i] - '0'); i++; }
    if (name[i] == 0 && i > (neg ? 1 : 0)) { push(neg ? -val : val); return 1; }
    return 0;
}

static void interpret(const char *line) {
    char word[WORD_SZ]; int wi = 0;
    for (int i = 0; line[i]; i++) {
        if (line[i] <= ' ') {
            if (wi > 0) { word[wi] = 0; exec_word(word); wi = 0; }
        } else {
            if (wi < WORD_SZ - 1) word[wi++] = line[i];
        }
    }
    if (wi > 0) { word[wi] = 0; exec_word(word); }
}

void forth_init(void) {
    ds_sp = 0; rs_sp = 0; input_pos = 0; forth_win_id = -1; dcnt = 0; forth_out_y = 0;
    reg(".", w_dot); reg("DROP", w_drop); reg("DUP", w_dup); reg("SWAP", w_swap);
    reg("OVER", w_over); reg("ROT", w_rot); reg("+", w_add); reg("-", w_sub);
    reg("*", w_mul); reg("/", w_div); reg("=", w_eq); reg("<", w_lt); reg(">", w_gt);
    reg(".S", w_dot_s); reg("EMIT", w_emit); reg("CR", w_cr); reg("BYE", w_bye);
}

void forth_open(void) {
    forth_win_id = wm_create(40, 20, 140, 160, tr(S_FORTH), forth_draw, forth_keypress, 0);
}

void forth_draw(int id) {
    window_t *win = wm_get(id);
    if (!win || !win->visible) { forth_win_id = -1; return; }
    forth_bx = win->x + 5; forth_by = win->y + 13;
    vga_drawstring(forth_bx, forth_by, "> ", 6, 1);
    vga_drawstring(forth_bx + 15, forth_by, input, 15, 1);
    vga_drawstring(forth_bx, forth_by + 10, tr(S_STACK), 7, 1);
    char b[8];
    for (int i = 0; i < ds_sp && i < 12; i++) {
        itoa(ds[i], b);
        vga_drawstring(forth_bx, forth_by + 20 + i * 9, b, 8, 1);
    }
}

void forth_keypress(int id, char c) {
    (void)id;
    if (c >= 32 && input_pos < 60) {
        input[input_pos++] = c; input[input_pos] = 0;
    } else if (c == 8 && input_pos > 0) {
        input[--input_pos] = 0;
    } else if (c == 13) {
        input[input_pos] = 0;
        forth_out_y = 0;
        interpret(input);
        input_pos = 0; input[0] = 0;
    }
}
