#include "kernel.h"

typedef struct { u16 base_lo, sel, zero, flags, base_hi; } __attribute__((packed)) idt_entry_t;
typedef struct { u16 limit; u32 base; } __attribute__((packed)) idt_ptr_t;

typedef struct {
    u16 prev, _0;
    u32 esp0;
    u16 ss0, _1;
    u32 esp1;
    u16 ss1, _2;
    u32 esp2;
    u16 ss2, _3;
    u32 cr3, eip, eflags, eax, ecx, edx, ebx, esp, ebp, esi, edi;
    u16 es, _4, cs, _5, ss, _6, ds, _7, fs, _8, gs, _9;
    u16 ldt, _10;
    u16 trap, iobase;
} __attribute__((packed)) tss_t;

typedef struct {
    u16 limit_low;
    u16 base_low;
    u8  base_mid;
    u8  type;
    u8  flags;
    u8  base_high;
} __attribute__((packed)) gdt_entry_t;

typedef struct {
    u16 limit;
    u32 base;
} __attribute__((packed)) gdt_ptr_t;

static tss_t tss;
static gdt_entry_t gdt[6];
static gdt_ptr_t gdt_p = { sizeof(gdt) - 1, (u32)gdt };

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
extern void isr16(), isr17();
extern void irq0(), irq1(), irq2(), irq3(), irq4(), irq5(), irq6(), irq7();
extern void irq8(), irq9(), irq10(), irq11(), irq12(), irq13();
extern void syscall_handler_asm(void);

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
        __asm__ volatile("cli");
        int next = (kb_raw_head + 1) & 255;
        if (next != kb_raw_tail) {
            kb_raw[kb_raw_head] = sc;
            kb_raw_head = next;
        }
        __asm__ volatile("sti");
    } else if (num == 44) {
        mouse_handler();
    } else if (num == 45) {
        inb(0xF0);
    }
}

static void gdt_set(int idx, u32 base, u32 limit, u8 type, u8 flags) {
    gdt[idx].limit_low = limit & 0xFFFF;
    gdt[idx].base_low = base & 0xFFFF;
    gdt[idx].base_mid = (base >> 16) & 0xFF;
    gdt[idx].type = type;
    gdt[idx].flags = (flags << 4) | ((limit >> 16) & 0x0F);
    gdt[idx].base_high = (base >> 24) & 0xFF;
}

static void tss_init(void) {
    for (int i = 0; i < sizeof(tss); i++) ((u8*)&tss)[i] = 0;
    tss.ss0 = 0x10;
    tss.esp0 = 0x100000;
    tss.iobase = sizeof(tss);
    u32 base = (u32)&tss;
    u32 limit = sizeof(tss) - 1;
    gdt_set(0, 0, 0, 0, 0);
    gdt_set(1, 0, 0xFFFFF, 0x9A, 0xC);
    gdt_set(2, 0, 0xFFFFF, 0x92, 0xC);
    gdt_set(3, 0, 0xFFFFF, 0xFA, 0xC);
    gdt_set(4, 0, 0xFFFFF, 0xF2, 0xC);
    gdt_set(5, base, limit, 0x89, 0x0);
    __asm__ volatile("lgdt %0\n"
                     "ljmp $0x08, $1f\n"
                     "1:\n"
                     "mov $0x10, %%ax\n"
                     "mov %%ax, %%ds\n"
                     "mov %%ax, %%es\n"
                     "mov %%ax, %%fs\n"
                     "mov %%ax, %%gs\n"
                     "mov %%ax, %%ss"
                     : : "m"(gdt_p) : "eax", "memory");
    __asm__ volatile("ltr %0" : : "r"((u16)0x28));
    u32 cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x01;
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));
}

void idt_init(void) {
    u32 base = 0x08;
    u32 handlers[] = {(u32)isr0,(u32)isr1,(u32)isr2,(u32)isr3,(u32)isr4,(u32)isr5,(u32)isr6,(u32)isr7,
                      (u32)isr8,(u32)isr9,(u32)isr10,(u32)isr11,(u32)isr12,(u32)isr13,(u32)isr14,(u32)isr15,
                      (u32)isr16,(u32)isr17};
    for (int i = 0; i < 18; i++)
        if (handlers[i]) idt_set(i, handlers[i], base, 0x8E);
    u32 irqs[] = {(u32)irq0,(u32)irq1,(u32)irq2,(u32)irq3,(u32)irq4,
                  (u32)irq5,(u32)irq6,(u32)irq7,(u32)irq8,(u32)irq9,
                  (u32)irq10,(u32)irq11,(u32)irq12,(u32)irq13};
    for (int i = 0; i < 14; i++) idt_set(32 + i, irqs[i], base, 0x8E);
    idt_set(0x80, (u32)syscall_handler_asm, 0x08, 0xEE);

    outb(0x20, 0x11); outb(0xA0, 0x11);
    outb(0x21, 0x20); outb(0xA1, 0x28);
    outb(0x21, 0x04); outb(0xA1, 0x02);
    outb(0x21, 0x01); outb(0xA1, 0x01);
    outb(0x21, 0xF8); outb(0xA1, 0xEF);

    __asm__ volatile("lidt %0" : : "m"(idt_p));
    tss_init();
}
