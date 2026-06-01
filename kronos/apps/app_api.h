#ifndef APP_API_H
#define APP_API_H

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;

typedef struct {
    int x, y, w, h;
    char title[24];
    int active, visible;
    void (*draw)(int id);
    void (*keypress)(int id, char c);
    void (*click)(int id, int mx, int my);
} window_t;

typedef struct {
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
    window_t *(*wm_get)(int id);
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
} app_api_t;

#endif
