#include "event_queue.h"

static volatile event_t queue[EV_QUEUE_SIZE];
static volatile int ev_head = 0;
static volatile int ev_tail = 0;
static volatile int ev_overflow_flag = 0;

void ev_init(void) {
    ev_head = 0;
    ev_tail = 0;
    ev_overflow_flag = 0;
}

int ev_push(event_t e) {
    __asm__ volatile("cli");
    int next = (ev_head + 1) & (EV_QUEUE_SIZE - 1);
    if (next == ev_tail) { ev_overflow_flag = 1; __asm__ volatile("sti"); return 0; }
    queue[ev_head] = e;
    ev_head = next;
    __asm__ volatile("sti");
    return 1;
}

int ev_pop(event_t *e) {
    __asm__ volatile("cli");
    if (ev_head == ev_tail) { __asm__ volatile("sti"); return 0; }
    *e = queue[ev_tail];
    ev_tail = (ev_tail + 1) & (EV_QUEUE_SIZE - 1);
    __asm__ volatile("sti");
    return 1;
}

int ev_empty(void) {
    return ev_head == ev_tail;
}

int ev_overflow(void) {
    int r = ev_overflow_flag;
    ev_overflow_flag = 0;
    return r;
}
