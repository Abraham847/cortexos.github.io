#include "kernel.h"

static u8 cycle = 0;

void kb_init(void) { kb_head = kb_tail = 0; }

char kb_getchar(void) {
    while (kb_head == kb_tail) __asm__ volatile("hlt");
    char c = kb_buf[kb_tail];
    kb_tail = (kb_tail + 1) & 255;
    return c;
}

void kb_gets(char *buf, int max) {
    int i = 0;
    while (i < max - 1) {
        char c = kb_getchar();
        if (c == '\n' || c == '\r') break;
        if (c == 8 && i > 0) i--;
        else if (c >= 32) buf[i++] = c;
    }
    buf[i] = 0;
}

int kb_keypressed(void) { return kb_head != kb_tail; }
