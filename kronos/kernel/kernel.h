#ifndef KERNEL_H
#define KERNEL_H

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

void vga_init(void);
void vga_putpixel(int x, int y, u8 col);
void vga_fill(u8 col);
void vga_drawrect(int x, int y, int w, int h, u8 col);
void vga_drawchar(int x, int y, u8 c, u8 fg, u8 bg);
void vga_drawstring(int x, int y, const char *s, u8 fg, u8 bg);
void vga_setpalette(int idx, int r, int g, int b);
void vga_init_palette(void);
void vga_fillrect(int x, int y, int w, int h, u8 col);
void vga_drawcircle(int cx, int cy, int r, u8 col);

void idt_init(void);

void task_init(void);
int task_create(void (*func)(void));
void task_yield(void);
void task_exit(void);
void task_schedule(void);
int task_count(void);

void kb_init(void);
char kb_getchar(void);
void kb_gets(char *buf, int max);
int kb_keypressed(void);

void mouse_init(void);
void mouse_handler(void);
extern volatile int mouse_x, mouse_y, mouse_btn;

void timer_init(void);
void timer_sleep(int ticks);
extern volatile u32 timer_ticks;

typedef struct {
    int x, y, w, h;
    char title[24];
    int active, visible;
    void (*draw)(int id);
    void (*keypress)(int id, char c);
    void (*click)(int id, int mx, int my);
} window_t;

#define MAX_WINS 12

void wm_init(void);
int wm_create(int x, int y, int w, int h, const char *title,
              void (*draw)(int), void (*keypress)(int, char), void (*click)(int, int, int));
void wm_close(int id);
void wm_draw(void);
void wm_handle_click(int mx, int my);
void wm_drag_move(int mx, int my);
void wm_handle_key(char c);
void wm_focus(int id);
window_t *wm_get(int id);

void desktop_init(void);
void desktop_draw(void);
void desktop_click(int mx, int my);

void shell_init(void);
void shell_keypress(int id, char c);
void shell_draw(int id);

void ai_init(void);
void ai_draw(int id);
void ai_keypress(int id, char c);
void ai_get_info(char *buf, int max);
void ai_create_net(int nl, int *sz);
int ai_train_net(int epochs, int lr_int);
int ai_save_model(const char *path);
int ai_load_model(const char *path);
int ai_export_txt(const char *path);
int ai_infer_str(const char *in_str, char *out_str, int max);
void ai_set_act(int layer, int act);
void ai_set_auto(int on);
void ai_set_lr(int lr_int);
void ai_set_epochs(int ep);
int ai_load_dataset(const char *path);
void ai_bg_task(void);
void ai_open_trainer(int si);
void ai_open_editor(int si);
void ai_open_ds_view(void);
void ai_open_weights(int si);
int ai_select_slot(int si);
int ai_get_slot_count(void);
int ai_slot_ready(int si);
int ai_infer_slot(int si, int *in, int *out);
int ai_save_model_slot(int si, const char *path);
int ai_load_model_slot(int si, const char *path);
int ai_export_text_ds(const char *path);
int ai_import_text_ds(const char *path, int ni, int no);
int ds_create_mgr(int ni, int no);
int ds_add_sample(const char *str);
int ds_save_mgr(const char *path);
int ds_mgr_count(void);
void ds_clear_mgr(void);

void ipc_init(void);
int ipc_send(int dst_tid, int type, const char *data, int len);
int ipc_recv(int src_filter, int *type, char *data, int *len);
int ipc_available(int src_filter);
int ipc_self(void);

struct nn_s;
typedef struct nn_s nn;
void model_init(void);
int model_register(const char *name, nn *net);
int model_load_file(const char *name, const char *path);
nn* model_get(const char *name);
int model_put(const char *name);
int model_unregister(const char *name);
void model_list(char *buf, int max);
int model_count(void);
const char *model_name_at(int idx);
int model_refs_at(int idx);

void aidemo_init(void);
void aidemo_task(void);

extern volatile char kb_buf[256];
extern volatile int kb_head, kb_tail;

extern u32 vga_fb_addr;
extern u16 vga_width, vga_height, vga_pitch;

void edit_init(void);
void edit_open(const char *fn);
void edit_draw(int id);
void edit_keypress(int id, char c);

void forth_init(void);
void forth_open(void);
void forth_draw(int id);
void forth_keypress(int id, char c);

void paint_init(void);
void paint_open(void);
void paint_draw(int id);
void paint_keypress(int id, char c);
void paint_click(int id, int mx, int my);

#endif
