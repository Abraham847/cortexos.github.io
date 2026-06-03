typedef unsigned char u8;
typedef unsigned short u16;

extern void vga_drawchar(int x, int y, u8 c, u8 fg, u8 bg);
extern void vga_fillrect(int x, int y, int w, int h, u8 col);
extern u32 vga_fb_addr;
extern u16 vga_width, vga_height, vga_pitch;

static int tty_x, tty_y;
static u8 tty_fg = 15, tty_bg = 1;

void tty_write_char(char c) {
    int cols = vga_width / 8, rows = vga_height / 8;
    if (c == '\n') { tty_y++; tty_x = 0; return; }
    if (c == '\r') { tty_x = 0; return; }
    if (c == '\t') { tty_x = (tty_x + 4) & ~3; return; }
    if (c == '\b') { if (tty_x > 0) tty_x--; return; }
    vga_drawchar(tty_x * 8, tty_y * 8, c, tty_fg, tty_bg);
    tty_x++;
    if (tty_x >= cols) { tty_x = 0; tty_y++; }
    if (tty_y >= rows) {
        u8 *fb = (u8*)vga_fb_addr;
        for (int y = 0; y < (rows - 1) * 8; y++)
            for (int x = 0; x < vga_width; x++)
                fb[y * vga_pitch + x] = fb[(y + 8) * vga_pitch + x];
        for (int y = (rows - 1) * 8; y < rows * 8; y++)
            for (int x = 0; x < vga_width; x++)
                fb[y * vga_pitch + x] = tty_bg;
        tty_y = rows - 1;
    }
}

void tty_write_str(const char *s) {
    while (*s) tty_write_char(*s++);
}

void tty_clear(void) {
    vga_fillrect(0, 0, vga_width, vga_height, tty_bg);
    tty_x = 0;
    tty_y = 0;
}
