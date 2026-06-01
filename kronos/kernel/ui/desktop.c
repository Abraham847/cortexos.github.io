#include "kernel.h"
#include "bios.h"
#include "lang.h"
#include "sysmon.h"
#include "fman.h"

#define ICON_N 5
static const char *icon_names[ICON_N] = {"Term", "Files", "AI", "Neural", "Mon"};
static int icon_sel;
static int desktop_dirty = 1;

void desktop_init(void) {
    icon_sel = -1;
    desktop_dirty = 1;
}

void desktop_mark_dirty(void) {
    desktop_dirty = 1;
}

void desktop_draw(void) {
    if (!desktop_dirty) return;
    desktop_dirty = 0;
    int bar_y = vga_height - 13;
    vga_fillrect(0, 0, vga_width, bar_y, 1);
    vga_fillrect(0, bar_y, vga_width, 13, 2);

    int icx = 5;
    for (int i = 0; i < ICON_N; i++) {
        vga_drawrect(icx, 5, 20, 18, icon_sel == i ? 11 : 6);
        vga_fillrect(icx + 1, 6, 18, 16, 1);
        vga_drawstring(icx + 2, 8, icon_names[i], 7, 1);
        icx += 25;
    }

    char buf[20];
    u32 mem = bios_total_mem_kb();
    itoa(mem / 1024, buf);
    vga_drawstring(5, 28, tr(S_RAM), 8, 1);
    vga_drawstring(35, 28, buf, 11, 1);
    vga_drawstring(55, 28, "MB", 8, 1);

    const char *vendor = bios_cpu_vendor();
    vga_drawstring(5, 38, tr(S_CPU), 8, 1);
    vga_drawstring(35, 38, vendor, 11, 1);

    itoa(timer_ticks / 100, buf);
    vga_drawstring(5, 48, tr(S_UPTIME), 8, 1);
    vga_drawstring(55, 48, buf, 11, 1);
    vga_drawstring(80, 48, "s", 8, 1);

    vga_drawrect(0, bar_y, vga_width, 13, 6);
    vga_fillrect(1, bar_y + 1, vga_width - 2, 11, 2);
    vga_drawstring(5, bar_y + 2, "[Menu]", 5, 2);
    vga_drawstring(55, bar_y + 2, tr(S_KRONOS), 7, 2);
    itoa(timer_ticks / 100, buf);
    vga_drawstring(100, bar_y + 2, tr(S_UPTIME), 8, 2);
    vga_drawstring(130, bar_y + 2, buf, 11, 2);
    vga_drawstring(155, bar_y + 2, "s", 8, 2);

    if (vga_width >= 640) vga_drawstring(180, bar_y + 2, "x86 32bit PMode", 8, 2);
}

void desktop_click(int mx, int my) {
    int bar_y = vga_height - 13;
    if (my >= bar_y) {
        if (mx < 50) {
            sysmon_open();
        } else {
            wm_create(20, 30, 180, 100, tr(S_TERMINAL), shell_draw, shell_keypress, 0);
        }
    } else if (my < 24) {
        int idx = (mx - 5) / 25;
        if (idx >= 0 && idx < ICON_N) {
            icon_sel = idx;
            switch (idx) {
                case 0: wm_create(20, 30, 180, 100, "Term", shell_draw, shell_keypress, 0); break;
                case 1: fman_open(); break;
                case 2: ai_open_trainer(0); break;
                case 3: ai_open_editor(0); break;
                case 4: sysmon_open(); break;
            }
        }
    }
}
