#ifndef EVENT_QUEUE_H
#define EVENT_QUEUE_H

#include "core.h"

typedef enum {
    EV_NONE = 0,
    EV_KEY_PRESS,
    EV_MOUSE_CLICK,
} event_type_t;

typedef struct {
    event_type_t type;
    char key_char;
    int mouse_x, mouse_y, mouse_btn;
} event_t;

#define EV_QUEUE_SIZE 512

void ev_init(void);
int ev_push(event_t e);
int ev_pop(event_t *e);
int ev_empty(void);
int ev_overflow(void);

#endif
