#ifndef VGA_H
#define VGA_H

#include "core.h"

void vga_init(void);
void vga_putpixel(int x, int y, u8 col);
void vga_fill(u8 col);
void vga_drawrect(int x, int y, int w, int h, u8 col);
void vga_drawchar(int x, int y, u8 c, u8 fg, u8 bg);
void vga_drawstring(int x, int y, const char *s, u8 fg, u8 bg);
void vga_setpalette(int idx, int r, int g, int b);
void vga_init_palette(void);
void vga_fillrect(int x, int y, int w, int h, u8 col);
void vga_drawcircle(int cx, int cy, int r, u8 col);

#endif
