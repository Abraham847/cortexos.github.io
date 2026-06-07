#include "kernel.h"
#include "task.h"

void timer_init(void) {
    timer_ticks = 0;
    outb(0x43, 0x36);
    u16 div = 1193;
    outb(0x40, div & 0xFF);
    outb(0x40, div >> 8);
}

void timer_sleep(int ticks) {
    u32 start = timer_ticks;
    while ((u32)(timer_ticks - start) < (u32)ticks) {
        if (task_is_initialized()) task_yield();
        else __asm__ volatile("hlt");
    }
}
