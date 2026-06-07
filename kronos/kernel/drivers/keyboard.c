#include "kernel.h"
#include "task.h"
#include "event_queue.h"

void kb_init(void) { kb_head = kb_tail = 0; kb_raw_head = kb_raw_tail = 0; }

void kb_process(void) {
    static int shift = 0, ctrl = 0;
    static int e0_prefix = 0;
    static int repeat_sc = 0;
    static u32 repeat_time = 0;
    static int repeat_phase = 0;
    static u8 map[] = {0,0,'1','2','3','4','5','6','7','8','9','0','-','=',8,9,
        'q','w','e','r','t','y','u','i','o','p','[',']',13,0,'a','s',
        'd','f','g','h','j','k','l',';',39,'`',0,92,'z','x','c','v',
        'b','n','m',',','.','/',0,'*',0,' '};
    static u8 map_s[] = {0,0,'!','@','#','$','%','^','&','*','(',')','_','+',8,9,
        'Q','W','E','R','T','Y','U','I','O','P','{','}',13,0,'A','S',
        'D','F','G','H','J','K','L',':',34,'~',0,'|','Z','X','C','V',
        'B','N','M','<','>','?',0,'*',0,' '};

    while (kb_raw_head != kb_raw_tail) {
        u8 sc = kb_raw[kb_raw_tail];
        kb_raw_tail = (kb_raw_tail + 1) & 255;

        if (sc == 0xE0) { e0_prefix = 1; continue; }

        if (sc == 0x2A || sc == 0x36) shift = 1;
        else if (sc == 0xAA || sc == 0xB6) shift = 0;
        if (sc == 0x1D) ctrl = 1;
        else if (sc == 0x9D) ctrl = 0;

        if (sc & 0x80) {
            if ((sc & 0x7F) == repeat_sc) repeat_sc = 0;
            e0_prefix = 0;
            continue;
        }

        repeat_sc = sc;
        repeat_time = timer_ticks;
        repeat_phase = 0;

        u8 c = 0;
        if (!e0_prefix) {
            if (sc == 0x48) c = 0x80;
            else if (sc == 0x50) c = 0x81;
            else if (sc == 0x4B) c = 0x82;
            else if (sc == 0x4D) c = 0x83;
            else if (ctrl && sc == 0x1E) c = 0x84;
            else if (ctrl && sc == 0x1F) c = 0x85;
            else if (ctrl && sc == 0x18) c = 0x86;
            else if (ctrl && sc == 0x10) c = 0x87;
            else if (sc < sizeof(map)) c = shift ? map_s[sc] : map[sc];
        } else {
            if (sc == 0x48) c = 0x80;
            else if (sc == 0x50) c = 0x81;
            else if (sc == 0x4B) c = 0x82;
            else if (sc == 0x4D) c = 0x83;
            if (ctrl && sc == 0x1C) c = 0x84;
            if (ctrl && sc == 0x1D) c = 0x85;
        }
        e0_prefix = 0;

        if (c) {
            int next = (kb_head + 1) & 255;
            if (next != kb_tail) {
                kb_buf[kb_head] = c;
                kb_head = next;
            }
            event_t e = { .type = EV_KEY_PRESS, .key_char = c };
            ev_push(e);
        }
    }

    if (repeat_sc) {
        u32 d = repeat_phase ? 5 : 50;
        if (timer_ticks - repeat_time >= d) {
            repeat_time = timer_ticks;
            repeat_phase = 1;
            u8 sc = (u8)repeat_sc;
            u8 c = 0;
            if (sc == 0x48) c = 0x80;
            else if (sc == 0x50) c = 0x81;
            else if (sc == 0x4B) c = 0x82;
            else if (sc == 0x4D) c = 0x83;
            else if (sc < sizeof(map)) c = shift ? map_s[sc] : map[sc];
            if (c) {
                int next = (kb_head + 1) & 255;
                if (next != kb_tail) {
                    kb_buf[kb_head] = c;
                    kb_head = next;
                }
                event_t e = { .type = EV_KEY_PRESS, .key_char = c };
                ev_push(e);
            }
        }
    }
}

char kb_getchar(void) {
    while (kb_head == kb_tail) {
        kb_process();
        if (kb_head == kb_tail) {
            if (task_is_initialized()) task_yield();
            else __asm__ volatile("hlt");
        }
    }
    char c = kb_buf[kb_tail];
    kb_tail = (kb_tail + 1) & 255;
    return c;
}

void kb_gets(char *buf, int max) {
    int i = 0;
    while (i < max - 1) {
        char c = kb_getchar();
        if (c == '\n' || c == '\r') break;
        if (c == 8 && i > 0) i--;
        else if (c >= 32) buf[i++] = c;
    }
    buf[i] = 0;
}

int kb_keypressed(void) { return kb_head != kb_tail; }
