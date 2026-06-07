#include "task.h"
#include "vga.h"

static void panic(const char *msg) {
    __asm__ volatile("cli");
    vga_fill(4);
    vga_drawstring(10, 10, msg, 15, 4);
    while (1) __asm__ volatile("hlt");
}

static task_t tasks[MAX_TASKS];
static int task_n;
static int current;
static int task_initialized;
extern u32 task_saved_esp;
extern volatile int task_switch_pending;
extern u32 task_new_esp;

static int fpu_initialized = 0;
static void fpu_init(void);

static int live_count(void) {
    int n = 0;
    for (int i = 0; i < MAX_TASKS; i++)
        if (tasks[i].state != TASK_FREE && tasks[i].state != TASK_DEAD) n++;
    return n;
}

static void check_canary(task_t *t) {
    u32 *bot = (u32*)t->stack;
    if (bot[0] != STACK_CANARY) panic("Stack overflow");
}

static int next_task(void) {
    int n = current;
    for (int i = 0; i < MAX_TASKS; i++) {
        n = (n + 1) % MAX_TASKS;
        if (tasks[n].state == TASK_READY) return n;
    }
    return current;
}

void task_init(void) {
    for (int i = 0; i < MAX_TASKS; i++) {
        tasks[i].state = TASK_FREE;
        tasks[i].id = -1;
        tasks[i].stack[0] = STACK_CANARY;
    }
    task_n = 0;
    current = 0;
    task_initialized = 1;
    task_switch_pending = 0;
    fpu_init();
}

int task_create(task_fn_t func) {
    if (!task_initialized || task_n >= MAX_TASKS) return -1;
    int si = 0;
    while (si < MAX_TASKS && tasks[si].state != TASK_FREE) si++;
    if (si >= MAX_TASKS) return -1;

    u32 *sp = tasks[si].stack + TASK_STACK_SIZE;
    sp -= 15;
    sp[0] = 0x10;  sp[1] = 0x10;
    sp[2] = 0;     sp[3] = 0;
    sp[4] = 0;     sp[5] = 0;
    sp[6] = 0;     sp[7] = 0;
    sp[8] = 0;     sp[9] = 0;
    sp[10] = 0;    sp[11] = 0;
    sp[12] = (u32)func;
    sp[13] = 0x08;
    sp[14] = 0x202;

    tasks[si].saved_esp = (u32)sp;
    tasks[si].state = TASK_READY;
    tasks[si].id = si;
    tasks[si].func = func;
    tasks[si].quantum = 0;
    task_n++;
    return si;
}

void fpu_init(void) {
    u32 cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~0x0C;
    cr0 |= 0x22;
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));
    __asm__ volatile("fninit");
    fpu_initialized = 1;
}

void task_schedule(void) {
    if (!task_initialized || live_count() < 1) return;

    check_canary(&tasks[current]);

    if (fpu_initialized)
        __asm__ volatile("fxsave %0" : "=m"(tasks[current].fpu_state));

    tasks[current].saved_esp = task_saved_esp;

    if (tasks[current].state != TASK_DEAD)
        tasks[current].state = TASK_READY;

    int next = next_task();

    check_canary(&tasks[next]);

    current = next;
    tasks[current].state = TASK_RUNNING;

    if (fpu_initialized)
        __asm__ volatile("fxrstor %0" : : "m"(tasks[current].fpu_state));

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

int task_is_initialized(void) {
    return task_initialized;
}
