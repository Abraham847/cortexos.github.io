#include "synch.h"
#include "task.h"

mutex_t mutex_fs = MUTEX_INIT;
mutex_t mutex_heap = MUTEX_INIT;
mutex_t mutex_ai = MUTEX_INIT;
mutex_t mutex_model = MUTEX_INIT;

void mutex_init(mutex_t *m) {
    m->locked = 0;
    m->holder = -1;
    m->depth = 0;
}

int mutex_lock(mutex_t *m) {
    int tid = current_task_id();
    u32 start = timer_ticks;
    while (1) {
        __asm__ volatile("cli");
        if (m->holder == tid) {
            m->depth++;
            __asm__ volatile("sti");
            return 0;
        }
        if (!m->locked) {
            m->locked = 1;
            m->holder = tid;
            m->depth = 1;
            __asm__ volatile("sti");
            return 0;
        }
        __asm__ volatile("sti");
        if (timer_ticks - start > 100) return -1;
        task_yield();
    }
}

void mutex_unlock(mutex_t *m) {
    __asm__ volatile("cli");
    if (m->holder == current_task_id()) {
        if (--m->depth == 0) {
            m->holder = -1;
            m->locked = 0;
        }
    }
    __asm__ volatile("sti");
}
