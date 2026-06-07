#include "kernel.h"
#include "event_queue.h"

static int mouse_cycle = 0;
static u8 mouse_packet[3];

static void mouse_wait(u8 type) {
    u32 timeout = 100000;
    if (type) while (timeout-- && (inb(0x64) & 2));
    else while (timeout-- && !(inb(0x64) & 1));
}

static u8 mouse_read(void) {
    mouse_wait(0);
    return inb(0x60);
}

static void mouse_write(u8 val) {
    mouse_wait(1);
    outb(0x64, 0xD4);
    mouse_wait(1);
    outb(0x60, val);
}

void mouse_init(void) {
    mouse_wait(1); outb(0x64, 0xA8);
    mouse_wait(1); outb(0x64, 0x20);
    mouse_wait(0); u8 status = inb(0x60) | 2;
    mouse_wait(1); outb(0x64, 0x60);
    mouse_wait(1); outb(0x60, status);
    mouse_write(0xF6); mouse_read();
    mouse_write(0xF4); mouse_read();
    mouse_x = vga_width / 2; mouse_y = vga_height / 2;
    mouse_btn = 0;
}

void mouse_handler(void) {
    static int prev_btn = 0;
    u8 byte = inb(0x60);
    switch (mouse_cycle) {
        case 0:
            if (!(byte & 0x08)) { mouse_cycle = 0; break; }
            mouse_packet[0] = byte; mouse_cycle = 1; break;
        case 1:
            mouse_packet[1] = byte; mouse_cycle = 2; break;
        case 2:
            mouse_packet[2] = byte; mouse_cycle = 0;
            int dx = (int)mouse_packet[1] - ((mouse_packet[0] & 0x10) ? 256 : 0);
            int dy = (int)mouse_packet[2] - ((mouse_packet[0] & 0x20) ? 256 : 0);
            mouse_x += dx; mouse_y -= dy;
            if (mouse_x < 0) mouse_x = 0; if (mouse_x >= (int)vga_width) mouse_x = vga_width - 1;
            if (mouse_y < 0) mouse_y = 0; if (mouse_y >= (int)vga_height) mouse_y = vga_height - 1;
            mouse_btn = mouse_packet[0] & 7;
            if (mouse_btn != prev_btn) {
                if (mouse_btn & 1) {
                    event_t e = { .type = EV_MOUSE_CLICK, .mouse_x = mouse_x, .mouse_y = mouse_y, .mouse_btn = mouse_btn };
                    ev_push(e);
                }
                prev_btn = mouse_btn;
            }
            break;
    }
}
