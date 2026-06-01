#include "kernel.h"

void timer_init(void) {
    timer_ticks = 0;
    outb(0x43, 0x36);
    u16 div = 1193;
    outb(0x40, div & 0xFF);
    outb(0x40, div >> 8);
}

void timer_sleep(int ticks) {
    u32 target = timer_ticks + ticks;
    while (timer_ticks < target) __asm__ volatile("hlt");
}
