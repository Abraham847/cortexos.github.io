#include "task.h"

static task_t tasks[MAX_TASKS];
static int task_n;
static int current;
static int task_initialized;

extern u32 task_saved_esp;
extern volatile int task_switch_pending;
extern u32 task_new_esp;

void task_init(void) {
    for (int i = 0; i < MAX_TASKS; i++) { tasks[i].state = TASK_FREE; tasks[i].id = -1; }
    task_n = 0;
    current = 0;
    task_initialized = 1;
    task_switch_pending = 0;
}

static int live_count(void) {
    int n = 0;
    for (int i = 0; i < MAX_TASKS; i++)
        if (tasks[i].state == TASK_READY || tasks[i].state == TASK_RUNNING) n++;
    return n;
}

int task_create(task_fn_t func) {
    int slot = task_n;
    for (int i = 0; i < MAX_TASKS; i++)
        if (tasks[i].state == TASK_FREE || tasks[i].state == TASK_DEAD) { slot = i; break; }
    if (slot >= MAX_TASKS) return -1;
    task_t *t = &tasks[slot];
    u32 *sp = (u32*)&t->stack[TASK_STACK_SIZE];

    *--sp = 0x202;
    *--sp = 0x08;
    *--sp = (u32)func;

    *--sp = 0;
    *--sp = 32;

    u32 int_num_addr = (u32)sp;

    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    *--sp = int_num_addr;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;

    *--sp = 0x10;
    *--sp = 0x10;

    t->saved_esp = (u32)sp;
    t->state = TASK_READY;
    t->id = slot;
    t->func = func;
    t->quantum = 5;
    if (slot >= task_n) task_n = slot + 1;
    return t->id;
}

static int next_task(void) {
    int n = live_count();
    if (n < 1) return current;
    for (int i = 0; i < task_n; i++) {
        int idx = (current + 1 + i) % task_n;
        if (tasks[idx].state == TASK_READY) return idx;
    }
    return current;
}

void task_schedule(void) {
    if (!task_initialized || live_count() < 1) return;

    if (tasks[current].state != TASK_DEAD)
        tasks[current].state = TASK_READY;

    int next = next_task();

    current = next;
    tasks[current].state = TASK_RUNNING;

    task_new_esp = tasks[current].saved_esp;
    task_switch_pending = 1;
}

void task_yield(void) {
    __asm__ volatile("int $0x20");
}

void task_exit(void) {
    tasks[current].state = TASK_DEAD;
    while (1) task_yield();
}

int task_count(void) {
    return live_count();
}

int current_task_id(void) {
    return task_initialized ? current : -1;
}
