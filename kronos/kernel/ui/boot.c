#include "kernel.h"
#include "boot.h"

static int boot_line;
static int boot_stage;

void boot_start(void) {
    vga_fill(0);
    boot_line = 0;
    boot_stage = 0;
    int cx = vga_width / 2, cy = vga_height / 2 - 40;
    vga_drawstring(cx - 40, cy - 16, "CORTEXOS", 5, 0);
    vga_drawstring(cx - 40, cy - 6, "v1.0 x86", 6, 0);
    for (int i = 0; i < 64; i++) vga_putpixel(cx - 32 + i, cy + 6, 5);
    vga_drawstring(cx - 40, cy + 16, "Loading", 8, 0);
}

void boot_msg(const char *s) {
    int cx = vga_width / 2;
    boot_stage++;
    int bx = cx - 32;
    for (int i = 0; i < 64 && i < boot_stage * 10; i++)
        vga_putpixel(bx + i, vga_height / 2 - 40 + 6, 11);
    vga_drawstring(cx - 40, vga_height / 2 - 40 + 26 + boot_line * 10, s, 7, 0);
    boot_line++;
}

void boot_done(void) {
    vga_fill(1);
}
