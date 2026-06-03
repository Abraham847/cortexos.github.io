/* libc: standard C library wrapping kernel API */

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

typedef struct _kernel_api {
    void (*putpixel)(int x, int y, u8 col);
    void (*fill)(u8 col);
    void (*drawrect)(int x, int y, int w, int h, u8 col);
    void (*fillrect)(int x, int y, int w, int h, u8 col);
    void (*drawcircle)(int cx, int cy, int r, u8 col);
    void (*drawchar)(int x, int y, u8 c, u8 fg, u8 bg);
    void (*drawstring)(int x, int y, const char *s, u8 fg, u8 bg);
    int  (*wm_create)(int x, int y, int w, int h, const char *title,
                      void (*draw)(int), void (*keypress)(int, char), void (*click)(int,int,int));
    void (*wm_close)(int id);
    void *(*wm_get)(int id);
    char (*kb_getchar)(void);
    int  (*kb_keypressed)(void);
    const char *(*tr)(int id);
    void (*task_yield)(void);
    int  (*mouse_x)(void);
    int  (*mouse_y)(void);
    int  (*mouse_btn)(void);
    void (*itoa)(int val, char *buf);
    int  (*strlen)(const char *s);
    int  (*strcmp)(const char *a, const char *b);
    void *(*kmalloc)(unsigned size);
    void (*kfree)(void *ptr);
    void (*vga_setpalette)(int idx, int r, int g, int b);
    int  (*screensize_x)(void);
    int  (*screensize_y)(void);
} kernel_api_t;

extern const void *__api_ptr;
#define api ((const kernel_api_t*)__api_ptr)

/* stdlib */
int atoi(const char *s) {
    int v = 0, sign = 1;
    while (*s == ' ') s++;
    if (*s == '-') { sign = -1; s++; } else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    return sign * v;
}

void *malloc(unsigned size) { return api->kmalloc(size); }
void free(void *ptr) { api->kfree(ptr); }

void itoa(int val, char *buf) { api->itoa(val, buf); }

/* string */
unsigned long strlen(const char *s) { return api->strlen(s); }
int strcmp(const char *a, const char *b) { return api->strcmp(a, b); }

char *strcpy(char *dst, const char *src) {
    char *d = dst;
    while ((*d++ = *src++));
    return dst;
}

char *strcat(char *dst, const char *src) {
    strcpy(dst + strlen(dst), src);
    return dst;
}

void *memcpy(void *dst, const void *src, unsigned long n) {
    char *d = (char*)dst;
    const char *s = (const char*)src;
    while (n--) *d++ = *s++;
    return dst;
}

void *memset(void *s, int c, unsigned long n) {
    char *p = (char*)s;
    while (n--) *p++ = (char)c;
    return s;
}

int memcmp(const void *a, const void *b, unsigned long n) {
    const char *ca = (const char*)a, *cb = (const char*)b;
    while (n--) { int d = *ca++ - *cb++; if (d) return d; }
    return 0;
}

/* stdio */
int putchar(int c) {
    static int tty_x, tty_y;
    int cols = api->screensize_x() / 8, rows = api->screensize_y() / 8;
    if (c == '\n') { tty_y++; tty_x = 0; return c; }
    api->drawchar(tty_x * 8, tty_y * 8, (unsigned char)c, 15, 1);
    tty_x++;
    if (tty_x >= cols) { tty_x = 0; tty_y++; }
    if (tty_y >= rows) tty_y = rows - 1;
    return c;
}

int puts(const char *s) {
    while (*s) putchar(*s++);
    putchar('\n');
    return 0;
}

static void print_num(unsigned v, int base) {
    char buf[32]; int i = 0;
    do { buf[i++] = "0123456789abcdef"[v % base]; v /= base; } while (v);
    while (i) putchar(buf[--i]);
}

int printf(const char *fmt, ...) {
    int *ap = (int*)(&fmt + 1);
    for (const char *p = fmt; *p; p++) {
        if (*p != '%') { putchar(*p); continue; }
        p++;
        if (*p == 'd') { int v = *ap++; if (v < 0) { putchar('-'); v = -v; } print_num(v, 10); }
        else if (*p == 'x' || *p == 'X') { print_num(*ap++, 16); }
        else if (*p == 's') { const char *s = (const char*)*ap++; while (*s) putchar(*s++); }
        else if (*p == 'c') { putchar(*ap++); }
        else if (*p == '%') { putchar('%'); }
    }
    return 0;
}

/* graphics wrappers */
void vga_putpixel(int x, int y, u8 col) { api->putpixel(x, y, col); }
void vga_fill(u8 col) { api->fill(col); }
void vga_drawrect(int x, int y, int w, int h, u8 col) { api->drawrect(x, y, w, h, col); }
void vga_fillrect(int x, int y, int w, int h, u8 col) { api->fillrect(x, y, w, h, col); }
void vga_drawchar(int x, int y, u8 c, u8 fg, u8 bg) { api->drawchar(x, y, c, fg, bg); }
void vga_drawstring(int x, int y, const char *s, u8 fg, u8 bg) { api->drawstring(x, y, s, fg, bg); }
