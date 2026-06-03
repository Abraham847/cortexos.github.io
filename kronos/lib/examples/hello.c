/* libc headers are in lib/libc/ */
#include "stdlib.h"
#include "string.h"

void main(void) {
    int x = 0;
    for (int i = 0; i < 100; i++) x += i;
    vga_drawstring(0, 0, "Hello from userspace!", 15, 1);
    char buf[32];
    itoa(x, buf);
    vga_drawstring(0, 16, "Sum 0-99 = ", 15, 1);
    vga_drawstring(96, 16, buf, 15, 1);
    void *p = malloc(64);
    itoa((int)p, buf);
    vga_drawstring(0, 32, "malloc(64) at ", 15, 1);
    vga_drawstring(96, 32, buf, 15, 1);
    vga_drawstring(0, 48, "libc: ", 15, 1);
    vga_drawstring(48, 48, strcat(strcpy(buf, "OK "), "strcpy works"), 15, 1);
}
