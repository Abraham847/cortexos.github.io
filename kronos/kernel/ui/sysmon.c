#include "kernel.h"
#include "ui/sysmon.h"
#include "bios.h"
#include "ui/lang.h"

static int sm_last_task = -1;
static int sm_last_uptime = -1;
static int sm_last_cpu_pct = -1;
static int sm_last_mem_pct = -1;

static void draw_bar(int x, int y, int w, int pct, u8 col) {
    if (pct > 100) pct = 100;
    if (pct < 0) pct = 0;
    vga_drawrect(x, y, w, 6, 8);
    int fw = (pct * (w - 2)) / 100;
    if (fw > 0) vga_fillrect(x + 1, y + 1, fw, 4, col);
}

void sysmon_open(void) {
    sm_last_task = -1;
    sm_last_uptime = -1;
    sm_last_cpu_pct = -1;
    sm_last_mem_pct = -1;
    wm_create(20, 80, 200, 180, "SysMon", sysmon_draw, sysmon_keypress, 0);
}

void sysmon_draw(int id) {
    int tc = task_count();
    int upt = timer_ticks / 100;
    int cpu_pct = tc * 12;
    if (cpu_pct > 95) cpu_pct = 95;
    int mem_pct = 30;

    if (tc == sm_last_task && upt == sm_last_uptime && cpu_pct == sm_last_cpu_pct && mem_pct == sm_last_mem_pct)
        return;

    sm_last_task = tc;
    sm_last_uptime = upt;
    sm_last_cpu_pct = cpu_pct;
    sm_last_mem_pct = mem_pct;

    window_t *win = wm_get(id);
    if (!win) return;
    int bx = win->x, by = win->y;
    int y = by + 15;
    char buf[20];
    vga_drawstring(bx + 5, y, "CortexOS v1.0", 5, 1); y += 12;
    vga_drawstring(bx + 5, y, "Mem:", 7, 1);
    extern u8 __bss_end[];
    itoa(128, buf);
    vga_drawstring(bx + 40, y, buf, 11, 1);
    vga_drawstring(bx + 60, y, "KB", 8, 1);
    y += 12;
    vga_drawstring(bx + 5, y, "Tasks:", 7, 1);
    itoa(tc, buf);
    vga_drawstring(bx + 50, y, buf, 11, 1);
    y += 12;
    vga_drawstring(bx + 5, y, "Uptime:", 7, 1);
    itoa(upt, buf);
    vga_drawstring(bx + 60, y, buf, 11, 1);
    vga_drawstring(bx + 95, y, "s", 8, 1);
    y += 14;
    vga_drawstring(bx + 5, y, "CPU", 7, 1);
    draw_bar(bx + 40, y, 80, cpu_pct, cpu_pct > 70 ? 12 : 10);
    char pbuf[8];
    itoa(cpu_pct, pbuf);
    vga_drawstring(bx + 125, y, pbuf, 11, 1);
    y += 10;
    vga_drawstring(bx + 5, y, "RAM", 7, 1);
    draw_bar(bx + 40, y, 80, mem_pct, 10);
    itoa(mem_pct, pbuf);
    vga_drawstring(bx + 125, y, pbuf, 11, 1);
    y += 14;
    vga_drawstring(bx + 5, y, "CPU:", 8, 1);
    const char *vendor = bios_cpu_vendor();
    vga_drawstring(bx + 40, y, vendor, 11, 1);
}

void sysmon_keypress(int id, char c) {
    (void)id;
    (void)c;
}
