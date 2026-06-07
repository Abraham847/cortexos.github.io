#include "kernel.h"
#include "bios.h"
#include "lang.h"
#include "sysmon.h"
#include "fman.h"
#include "rtc.h"

#define ICON_N 9
static const char *icon_names[ICON_N] = {"Term", "Files", "Studio", "NewAI", "Train", "Lab", "Mon", "DS", "View"};
static int icon_sel;
static int desktop_dirty = 1;
static char dtmp[16];

void desktop_init(void) {
    icon_sel = -1;
    desktop_dirty = 1;
}

void desktop_mark_dirty(void) {
    desktop_dirty = 1;
}

static void fmt_time(char *buf, int max) {
    rtc_time_t t;
    rtc_read(&t);
    int p = 0;
    if (t.hour < 10 && p < max - 1) buf[p++] = '0';
    char tmp[4];
    itoa(t.hour, tmp);
    for (int i = 0; tmp[i] && p < max - 1; i++) buf[p++] = tmp[i];
    if (p < max - 1) buf[p++] = ':';
    if (t.min < 10 && p < max - 1) buf[p++] = '0';
    itoa(t.min, tmp);
    for (int i = 0; tmp[i] && p < max - 1; i++) buf[p++] = tmp[i];
    if (p < max - 1) buf[p++] = ':';
    if (t.sec < 10 && p < max - 1) buf[p++] = '0';
    itoa(t.sec, tmp);
    for (int i = 0; tmp[i] && p < max - 1; i++) buf[p++] = tmp[i];
    buf[p] = 0;
}

static void fmt_date(char *buf, int max) {
    rtc_time_t t;
    rtc_read(&t);
    int p = 0;
    itoa(t.year, dtmp);
    for (int i = 0; dtmp[i] && p < max - 1; i++) buf[p++] = dtmp[i];
    if (p < max - 1) buf[p++] = '-';
    if (t.mon < 10 && p < max - 1) buf[p++] = '0';
    itoa(t.mon, dtmp);
    for (int i = 0; dtmp[i] && p < max - 1; i++) buf[p++] = dtmp[i];
    if (p < max - 1) buf[p++] = '-';
    if (t.day < 10 && p < max - 1) buf[p++] = '0';
    itoa(t.day, dtmp);
    for (int i = 0; dtmp[i] && p < max - 1; i++) buf[p++] = dtmp[i];
    buf[p] = 0;
}

void desktop_draw(void) {
    if (!desktop_dirty) return;
    desktop_dirty = 0;
    int bar_y = vga_height - 13;
    vga_fillrect(0, 0, vga_width, bar_y, 1);
    vga_fillrect(0, bar_y, vga_width, 13, 2);

    int icx = 5;
    for (int i = 0; i < ICON_N; i++) {
        vga_drawrect(icx, 5, 20, 18, icon_sel == i ? 12 : 7);
        vga_fillrect(icx + 1, 6, 18, 16, 1);
        vga_drawstring(icx + 2, 8, icon_names[i], 7, 1);
        icx += 25;
    }

    char buf[20];
    u32 mem = bios_total_mem_kb();
    itoa(mem / 1024, buf);
    vga_drawstring(5, 28, tr(S_RAM), 8, 1);
    vga_drawstring(5, 38, buf, 11, 1);
    vga_drawstring(35, 38, "MB", 8, 1);

    const char *vendor = bios_cpu_vendor();
    vga_drawstring(5, 48, tr(S_CPU), 8, 1);
    vga_drawstring(5, 58, vendor, 11, 1);

    vga_drawstring(120, 28, "Date", 8, 1);
    fmt_date(buf, 20);
    vga_drawstring(120, 38, buf, 11, 1);

    fmt_time(buf, 20);
    vga_drawstring(120, 48, "Time", 8, 1);
    vga_drawstring(120, 58, buf, 11, 1);

    itoa(timer_ticks / 100, buf);
    vga_drawstring(220, 28, tr(S_UPTIME), 8, 1);
    vga_drawstring(220, 38, buf, 11, 1);
    vga_drawstring(260, 38, "s", 8, 1);

    vga_drawrect(0, bar_y, vga_width, 13, 6);
    vga_fillrect(1, bar_y + 1, vga_width - 2, 11, 2);
    vga_drawstring(5, bar_y + 2, "[Menu]", 5, 2);
    vga_drawstring(55, bar_y + 2, tr(S_KRONOS), 7, 2);

    fmt_time(buf, 20);
    int tlen = 0; while (buf[tlen]) tlen++;
    vga_drawstring(vga_width - tlen * 8 - 8, bar_y + 2, buf, 11, 2);

    if (vga_width >= 640) vga_drawstring(180, bar_y + 2, "x86 32bit PMode", 8, 2);
}

static int find_shell_win(void) {
    for (int i = 0; i < 12; i++) {
        window_t *w = wm_get(i);
        if (w && w->visible && strcmp(w->title, tr(S_TERMINAL)) == 0)
            return i;
    }
    return -1;
}

static void open_or_focus_shell(void) {
    int id = find_shell_win();
    if (id >= 0) { wm_focus(id); return; }
    wm_create(20, 30, 180, 100, tr(S_TERMINAL), shell_draw, shell_keypress, 0);
}

void desktop_click(int mx, int my) {
    int bar_y = vga_height - 13;
    if (my >= bar_y) {
        if (mx < 50) {
            sysmon_open();
        } else {
            open_or_focus_shell();
        }
    } else if (my < 24) {
        int idx = (mx - 5) / 25;
        if (idx >= 0 && idx < ICON_N) {
            icon_sel = idx;
            switch (idx) {
                case 0: wm_create(20, 30, 180, 100, "Term", shell_draw, shell_keypress, 0); break;
                case 1: fman_open(); break;
                case 2: wm_create(5, 10, 280, 195, "AI Studio", aistudio_draw, aistudio_keypress, aistudio_click); break;
                case 3: wm_create(10, 10, 300, 410, "AI Creator", aicreator_draw, aicreator_keypress, aicreator_click); break;
                case 4: ai_open_trainer(0); break;
                case 5: wm_create(10, 20, 210, 180, "AI Lab", ailab_draw, ailab_keypress, ailab_click); break;
                case 6: sysmon_open(); break;
                case 7: ai_open_ds_view(); break;
                case 8: nnview_open(); break;
            }
        }
    }
}
