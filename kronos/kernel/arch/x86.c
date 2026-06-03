#include "x86.h"

void outb(u16 port, u8 val) { __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port)); }
u8 inb(u16 port) { u8 v; __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port)); return v; }
void outw(u16 port, u16 val) { __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port)); }
u16 inw(u16 port) { u16 v; __asm__ volatile("inw %1, %0" : "=a"(v) : "Nd"(port)); return v; }
void outl(u16 port, u32 val) { __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port)); }
u32 inl(u16 port) { u32 v; __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(port)); return v; }
