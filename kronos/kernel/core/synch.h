#ifndef SYNCH_H
#define SYNCH_H

#include "core.h"

typedef struct {
    volatile int locked;
    volatile int holder;
    volatile int depth;
} mutex_t;

#define MUTEX_INIT { 0 }

extern mutex_t mutex_fs;
extern mutex_t mutex_heap;
extern mutex_t mutex_ai;
extern mutex_t mutex_model;

void mutex_init(mutex_t *m);
int mutex_lock(mutex_t *m);
void mutex_unlock(mutex_t *m);

#endif
