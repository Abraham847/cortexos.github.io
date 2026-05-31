#include "kernel.h"
#include "bios.h"
#include "lang.h"

static int frame;

void desktop_init(void) {}

void desktop_draw(void) {
    frame++;
    int bar_y = vga_height - 13;
    if ((frame % 10) == 0) vga_fillrect(0, 0, vga_width, bar_y, 1);

    vga_drawstring(5, 5, tr(S_KRONOS), 7, 1);
    vga_drawstring(5, 15, "BIOS: INT 0x15 E820/E801", 8, 1);

    char buf[20];
    u32 mem = bios_total_mem_kb();
    itoa(mem / 1024, buf);
    vga_drawstring(5, 25, tr(S_RAM), 8, 1);
    vga_drawstring(35, 25, buf, 11, 1);
    vga_drawstring(55, 25, "MB", 8, 1);

    int count = *BIOS_MMAP_COUNT;
    itoa(count, buf);
    vga_drawstring(5, 35, "E820 regions: ", 8, 1);
    vga_drawstring(90, 35, buf, 11, 1);

    const char *vendor = bios_cpu_vendor();
    vga_drawstring(5, 45, tr(S_CPU), 8, 1);
    vga_drawstring(35, 45, vendor, 11, 1);

    vga_drawstring(5, bar_y - 2, tr(S_TASKBAR), 8, 1);

    vga_drawrect(0, bar_y, vga_width, 13, 6);
    vga_fillrect(1, bar_y + 1, vga_width - 2, 11, 2);
    vga_drawstring(5, bar_y + 2, " ", 7, 2);
    vga_drawstring(10, bar_y + 2, tr(S_KRONOS), 7, 2);

    itoa(timer_ticks / 100, buf);
    vga_drawstring(100, bar_y + 2, tr(S_UPTIME), 8, 2);
    vga_drawstring(130, bar_y + 2, buf, 11, 2);
    vga_drawstring(155, bar_y + 2, "s", 8, 2);

    if (vga_width >= 640) vga_drawstring(180, bar_y + 2, "x86 32bit PMode", 8, 2);
}

void desktop_click(int mx, int my) {
    int bar_y = vga_height - 13;
    if (my >= bar_y)
        wm_create(20, 30, 180, 100, tr(S_TERMINAL), shell_draw, shell_keypress, 0);
}
