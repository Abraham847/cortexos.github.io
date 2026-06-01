#include "kernel.h"
#include "bios.h"
#include "heap.h"
#include "ata.h"
#include "fs.h"
#include "task.h"
#include "aidemo.h"
#include "lang.h"
#include "boot.h"
#include "sysmon.h"
#include "fman.h"
#include "event_queue.h"

void outb(u16 port, u8 val) { __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port)); }
u8 inb(u16 port) { u8 v; __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port)); return v; }
void outw(u16 port, u16 val) { __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port)); }
u16 inw(u16 port) { u16 v; __asm__ volatile("inw %1, %0" : "=a"(v) : "Nd"(port)); return v; }

void memset(void *ptr, u8 val, u32 size) {
    u8 *p = (u8*)ptr; for (u32 i = 0; i < size; i++) p[i] = val;
}
void memcpy(void *dst, const void *src, u32 size) {
    u8 *d = (u8*)dst; const u8 *s = (const u8*)src;
    for (u32 i = 0; i < size; i++) d[i] = s[i];
}
int strlen(const char *s) { const char *p = s; while (*p) p++; return p - s; }
int strcmp(const char *a, const char *b) {
    if (!a) return !b ? 0 : -1;
    if (!b) return 1;
    while (*a && *a == *b) { a++; b++; }
    return *(u8*)a - *(u8*)b;
}
void itoa(int val, char *buf) {
    char tmp[16]; int i = 0, neg = 0;
    unsigned u;
    if (val < 0) { neg = 1; u = -(unsigned)val; }
    else { u = (unsigned)val; }
    if (u == 0) { buf[0] = '0'; buf[1] = 0; return; }
    while (u) { tmp[i++] = '0' + (u % 10); u /= 10; }
    int pos = 0;
    if (neg) buf[pos++] = '-';
    while (i) buf[pos++] = tmp[--i];
    buf[pos] = 0;
}
void itohex(u32 val, char *buf) {
    const char *hex = "0123456789ABCDEF";
    buf[0] = '0'; buf[1] = 'x';
    for (int i = 7; i >= 0; i--) { buf[2 + i] = hex[val & 0xF]; val >>= 4; }
    buf[10] = 0;
}

volatile u32 timer_ticks = 0;
volatile int mouse_x = 160, mouse_y = 100, mouse_btn = 0;
volatile int mouse_click_count = 0;
volatile char kb_buf[256];
volatile int kb_head = 0, kb_tail = 0;
volatile u8 kb_raw[256];
volatile int kb_raw_head = 0, kb_raw_tail = 0;

u32 vga_fb_addr = 0xA0000;
u16 vga_width = 320;
u16 vga_height = 200;
u16 vga_pitch = 320;

extern u8 __bss_start[], __bss_end[];

void ai_bg_task(void);

static int lw(const char *s) { return strlen(s) * 9; }

static int rlbl(const char *s, int x, int y, u8 fg) {
    vga_drawstring(x, y, s, fg, 1);
    return x + lw(s) + 5;
}

static void read_pw(char *buf, int max, int x, int y) {
    int bi = 0;
    while (1) {
        int c = kb_getchar();
        if (c == 13) { buf[bi] = 0; return; }
        if (c == 8 && bi > 0) { bi--; vga_drawchar(x + bi * 9, y, ' ', 7, 1); }
        else if (c >= 32 && bi < max - 1) { buf[bi++] = c; vga_drawchar(x + (bi-1) * 9, y, '*', 10, 1); }
    }
}

static void do_login(void) {
    char stored[16] = {0};
    int has_pw;
    while (kb_keypressed()) kb_getchar();

    has_pw = fs_read("PASS.SYS", (u8*)stored, 15) > 0;
    if (has_pw) {
        int sl = strlen(stored);
        while (sl > 0 && (stored[sl-1] == '\n' || stored[sl-1] == '\r')) stored[--sl] = 0;
    }

    if (!has_pw) {
        int cx = vga_width/2, cy = vga_height/2;
        vga_fill(1);
        vga_drawstring(cx - lw(tr(S_FIRST_SETUP))/2, cy-25, tr(S_FIRST_SETUP), 14, 1);
        int ix = rlbl(tr(S_CREATE_PASS), cx-70, cy-5, 7);
        char a[16], b[16];
        read_pw(a, 16, ix, cy-5);
        ix = rlbl(tr(S_AGAIN), cx-70, cy+10, 7);
        read_pw(b, 16, ix, cy+10);
        if (strcmp(a, b) != 0 || strlen(a) < 1) {
            vga_drawstring(cx - lw(tr(S_MISMATCH))/2, cy+30, tr(S_MISMATCH), 12, 1);
            kb_getchar();
            return do_login();
        }
        fs_write("PASS.SYS", (const u8*)a, strlen(a));
        memcpy(stored, a, 16);
    }

    int attempts = 3;
    while (attempts > 0) {
        int cx = vga_width/2, cy = vga_height/2;
        vga_fill(1);
        vga_drawstring(cx - lw(tr(S_KRONOS))/2, cy-30, tr(S_KRONOS), 14, 1);
        vga_drawstring(cx - lw(tr(S_LOGIN))/2, cy-15, tr(S_LOGIN), 11, 1);
        int ix = rlbl(tr(S_PASS), cx-60, cy+5, 7);
        char buf[16];
        read_pw(buf, 16, ix, cy+5);
        if (strcmp(buf, stored) == 0) { vga_fill(1); return; }
        attempts--;
        vga_drawstring(cx - lw(tr(S_INCORRECT))/2, cy+25, tr(S_INCORRECT), 12, 1);
        if (attempts > 0) {
            vga_drawstring(cx - lw(tr(S_ANY_KEY))/2, cy+35, tr(S_ANY_KEY), 8, 1);
            kb_getchar();
            while (kb_keypressed()) kb_getchar();
        }
    }
    vga_fill(0);
    vga_drawstring(vga_width/2 - lw(tr(S_LOCKED))/2, vga_height/2-4, tr(S_LOCKED), 12, 1);
    while (1) __asm__ volatile("hlt");
}

void kmain(void) {
    boot_start();
    heap_init(); boot_msg("heap");

    if (*BIOS_VBE_ACTIVE) {
        vga_fb_addr = *BIOS_VBE_FB;
        vga_width = *BIOS_VBE_WIDTH;
        vga_height = *BIOS_VBE_HEIGHT;
        vga_pitch = *BIOS_VBE_PITCH;
    }
    vga_init(); boot_msg("vga");
    vga_init_palette();

    idt_init(); boot_msg("idt");
    __asm__ volatile("sti");
    ev_init(); boot_msg("evq");
    ata_init(); boot_msg("ata");
    kb_init(); boot_msg("kbd");
    mouse_init(); boot_msg("mouse");
    timer_init(); boot_msg("timer");
    fs_init(); boot_msg("fs");

    boot_done();

    do_login();

    desktop_init();
    wm_init();
    shell_init();
    ai_init();
    ipc_init();
    model_init();
    aidemo_init();
    edit_init();
    forth_init();
    paint_init();

    wm_draw();

    task_init();
    task_create(ai_bg_task);
    task_create(aidemo_task);

    int dirty = 1;

    int last_uptime = -1;

    while (1) {
        kb_process();

        event_t e;
        while (ev_pop(&e)) {
            dirty = 1;
            switch (e.type) {
                case EV_KEY_PRESS:
                    wm_handle_key(e.key_char);
                    break;
                default:
                    break;
            }
        }

        if (mouse_click_count) {
            __asm__ volatile("cli");
            int n = mouse_click_count;
            int mx = mouse_x, my = mouse_y, mb = mouse_btn;
            mouse_click_count = 0;
            __asm__ volatile("sti");
            for (int i = 0; i < n; i++) {
                wm_handle_click(mx, my);
                desktop_click(mx, my);
            }
            dirty = 1;
        }

        if (mouse_btn & 1) {
            __asm__ volatile("cli");
            int mx = mouse_x, my = mouse_y;
            __asm__ volatile("sti");
            wm_drag_move(mx, my);
            dirty = 1;
        }

        int upt = timer_ticks / 100;
        if (upt != last_uptime) {
            last_uptime = upt;
            desktop_mark_dirty();
            dirty = 1;
        }

        if (dirty) {
            desktop_draw();
            wm_draw();
            __asm__ volatile("cli");
            int mx = mouse_x, my = mouse_y;
            __asm__ volatile("sti");
            vga_putpixel(mx, my, 15);
            if (mx > 0) vga_putpixel(mx - 1, my, 15);
            vga_putpixel(mx, my - 1, 15);
            if (mx < vga_width - 1) vga_putpixel(mx + 1, my, 15);
            vga_putpixel(mx, my + 1, 15);
            dirty = 0;
        }

        if (!task_is_initialized()) {
            __asm__ volatile("hlt");
        } else if (task_count() <= 1) {
            __asm__ volatile("hlt");
        } else {
            task_yield();
        }
    }
}
