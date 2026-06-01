#include "kernel.h"

typedef struct { u16 base_lo, sel, zero, flags, base_hi; } __attribute__((packed)) idt_entry_t;
typedef struct { u16 limit; u32 base; } __attribute__((packed)) idt_ptr_t;

static idt_entry_t idt[256];
static idt_ptr_t idt_p = { sizeof(idt) - 1, (u32)idt };

static void idt_set(int num, u32 handler, u16 sel, u8 flags) {
    idt[num].base_lo = handler & 0xFFFF;
    idt[num].sel = sel;
    idt[num].zero = 0;
    idt[num].flags = flags;
    idt[num].base_hi = (handler >> 16) & 0xFFFF;
}

extern void isr0(), isr1(), isr2(), isr3(), isr4(), isr5(), isr6(), isr7();
extern void isr8(), isr9(), isr10(), isr11(), isr12(), isr13(), isr14(), isr15();
extern void irq0(), irq1(), irq2(), irq3(), irq4(), irq5(), irq6(), irq7();
extern void irq8(), irq9(), irq10(), irq11(), irq12();

static char *exc_msg[] = {
    "Division by Zero", "Debug", "NMI", "Breakpoint", "Overflow",
    "Bound Range", "Invalid Opcode", "Device Not Avail", "Double Fault",
    "Coprocessor Seg", "Invalid TSS", "Seg Not Present", "Stack Fault",
    "GPF", "Page Fault", "Reserved", "x87 FPU", "Alignment", "Machine Check"
};

void isr_handler_c(int num) {
    if (num < 18) {
        vga_drawstring(10, 10, "EXCEPTION: ", 12, 1);
        vga_drawstring(100, 10, exc_msg[num], 12, 1);
    }
    while (1) __asm__ volatile("hlt");
}

void irq_handler_c(int num) {
    if (num == 32) {
        timer_ticks++;
        task_schedule();
    } else if (num == 33) {
        u8 sc = inb(0x60);
        int next = (kb_raw_head + 1) & 255;
        if (next != kb_raw_tail) {
            kb_raw[kb_raw_head] = sc;
            kb_raw_head = next;
        }
    } else if (num == 44) {
        mouse_handler();
    }
}

void idt_init(void) {
    u32 base = 0x08;
    u32 handlers[] = {(u32)isr0,(u32)isr1,(u32)isr2,(u32)isr3,(u32)isr4,(u32)isr5,(u32)isr6,(u32)isr7,
                      (u32)isr8,0,(u32)isr10,(u32)isr11,(u32)isr12,(u32)isr13,(u32)isr14,0};
    for (int i = 0; i < 16; i++)
        if (handlers[i]) idt_set(i, handlers[i], base, 0x8E);
    u32 irqs[] = {(u32)irq0,(u32)irq1,(u32)irq2,(u32)irq3,(u32)irq4,
                  (u32)irq5,(u32)irq6,(u32)irq7,(u32)irq8,(u32)irq9,
                  (u32)irq10,(u32)irq11,(u32)irq12};
    for (int i = 0; i < 13; i++) idt_set(32 + i, irqs[i], base, 0x8E);

    outb(0x20, 0x11); outb(0xA0, 0x11);
    outb(0x21, 0x20); outb(0xA1, 0x28);
    outb(0x21, 0x04); outb(0xA1, 0x02);
    outb(0x21, 0x01); outb(0xA1, 0x01);
    outb(0x21, 0xF8); outb(0xA1, 0xEF);

    __asm__ volatile("lidt %0" : : "m"(idt_p));
}
