#include "kernel_api.h"
#include "lang.h"
#include "heap.h"

static int get_mx(void) { return mouse_x; }
static int get_my(void) { return mouse_y; }
static int get_mb(void) { return mouse_btn; }

const kernel_api_t kernel_api = {
    .putpixel = vga_putpixel,
    .fill = vga_fill,
    .drawrect = vga_drawrect,
    .fillrect = vga_fillrect,
    .drawcircle = vga_drawcircle,
    .drawchar = vga_drawchar,
    .drawstring = vga_drawstring,
    .wm_create = wm_create,
    .wm_close = wm_close,
    .wm_get = wm_get,
    .kb_getchar = kb_getchar,
    .kb_keypressed = kb_keypressed,
    .tr = (const char *(*)(int))tr,
    .task_yield = task_yield,
    .mouse_x = get_mx,
    .mouse_y = get_my,
    .mouse_btn = get_mb,
    .itoa = itoa,
    .strlen = strlen,
    .strcmp = strcmp,
    .kmalloc = kmalloc,
    .kfree = kfree,
    .vga_setpalette = vga_setpalette,
};
