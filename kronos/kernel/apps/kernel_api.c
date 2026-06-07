#include "kernel_api.h"
#include "lang.h"
#include "heap.h"
#include "vga.h"
#include "kb.h"
#include "task.h"
#include "mouse.h"

static int is_user_ptr(const void *p) {
    u32 addr = (u32)p;
    if (addr >= 0x7E00 && addr < 0x7E00 + 0x10000) return 1;
    return 0;
}

static int get_mx(void) { return mouse_x; }
static int get_my(void) { return mouse_y; }
static int get_mb(void) { return mouse_btn; }

static void *safe_kmalloc(unsigned size) {
    if (size == 0 || size > 0x10000) return 0;
    return kmalloc(size);
}

static int get_sx(void) { return vga_width; }
static int get_sy(void) { return vga_height; }

static void safe_kfree(void *ptr) {
    if (!ptr) return;
    kfree(ptr);
}

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
    .kmalloc = safe_kmalloc,
    .kfree = safe_kfree,
    .vga_setpalette = vga_setpalette,
    .screensize_x = get_sx,
    .screensize_y = get_sy,
};
