#include "event_queue.h"

static volatile event_t queue[EV_QUEUE_SIZE];
static volatile int ev_head = 0;
static volatile int ev_tail = 0;

void ev_init(void) {
    ev_head = 0;
    ev_tail = 0;
}

int ev_push(event_t e) {
    int next = (ev_head + 1) & (EV_QUEUE_SIZE - 1);
    if (next == ev_tail) return 0;
    queue[ev_head] = e;
    ev_head = next;
    return 1;
}

int ev_pop(event_t *e) {
    if (ev_head == ev_tail) return 0;
    *e = queue[ev_tail];
    ev_tail = (ev_tail + 1) & (EV_QUEUE_SIZE - 1);
    return 1;
}

int ev_empty(void) {
    return ev_head == ev_tail;
}
