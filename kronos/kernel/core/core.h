#ifndef CORE_H
#define CORE_H

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;

#define NULL 0

void outb(u16 port, u8 val);
u8 inb(u16 port);
void outw(u16 port, u16 val);
u16 inw(u16 port);

void memset(void *ptr, u8 val, u32 size);
void memcpy(void *dst, const void *src, u32 size);
int strlen(const char *s);
int strcmp(const char *a, const char *b);
void itoa(int val, char *buf);
void itohex(u32 val, char *buf);

extern volatile u32 timer_ticks;
extern volatile int mouse_x, mouse_y, mouse_btn;
extern volatile int mouse_click_count;
extern volatile char kb_buf[256];
extern volatile int kb_head, kb_tail;
extern volatile u8 kb_raw[256];
extern volatile int kb_raw_head, kb_raw_tail;
extern u32 vga_fb_addr;
extern u16 vga_width, vga_height, vga_pitch;

#endif