#include "process.h"
#include "vfs.h"
#include "synch.h"
#include "vga.h"
#include "heap.h"
#include "task.h"
#include "elf.h"

static process_t procs[MAX_PROC];
static int proc_next_id;
static int proc_initialized;
static pid_t current_pid;
static pid_t next_pid;

static mutex_t proc_mutex = MUTEX_INIT;

extern u32 task_saved_esp;
extern volatile int task_switch_pending;
extern u32 task_new_esp;

static void panic(const char *msg) {
    __asm__ volatile("cli");
    vga_fill(4);
    vga_drawstring(10, 10, msg, 15, 4);
    while (1) __asm__ volatile("hlt");
}

static int fpu_initialized;
static void fpu_init(void) {
    u32 cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~0x0C; cr0 |= 0x22;
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));
    __asm__ volatile("fninit");
    fpu_initialized = 1;
}

static int live_count(void) {
    int n = 0;
    for (int i = 0; i < MAX_PROC; i++)
        if (procs[i].state != PROC_FREE && procs[i].state != PROC_DEAD) n++;
    return n;
}

static void check_canary(process_t *p) {
    u32 *bot = (u32*)p->stack;
    if (bot[0] != STACK_CANARY) panic("Stack overflow");
}

static int next_task(void) {
    int n = current_pid;
    for (int i = 0; i < MAX_PROC; i++) {
        n = (n + 1) % MAX_PROC;
        if (procs[n].state == PROC_READY) return n;
    }
    return current_pid;
}

void proc_init(void) {
    for (int i = 0; i < MAX_PROC; i++) {
        procs[i].state = PROC_FREE;
        procs[i].pid = -1;
        procs[i].stack[0] = STACK_CANARY;
    }
    proc_next_id = 0;
    current_pid = 0;
    next_pid = 1;
    proc_initialized = 1;
    task_switch_pending = 0;
    fpu_init();
}

pid_t proc_spawn(proc_fn_t func, const char *name) {
    if (!proc_initialized || proc_next_id >= MAX_PROC) return -1;
    mutex_lock(&proc_mutex);
    int si = 0;
    while (si < MAX_PROC && procs[si].state != PROC_FREE) si++;
    if (si >= MAX_PROC) { mutex_unlock(&proc_mutex); return -1; }

    u32 *sp = procs[si].stack + 4096;
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

    procs[si].pid = next_pid++;
    procs[si].ppid = current_pid;
    procs[si].uid = 0;
    procs[si].gid = 0;
    procs[si].saved_esp = (u32)sp;
    procs[si].state = PROC_READY;
    procs[si].func = func;
    procs[si].quantum = 0;
    procs[si].priority = 128;
    procs[si].page_dir = 0;
    for (int f = 0; f < MAX_FDS; f++) procs[si].fds[f].used = 0;
    for (int s = 0; s < 32; s++) { procs[si].signals[s].handler = 0; procs[si].signals[s].pending = 0; }

    if (name) {
        int j = 0;
        while (name[j] && j < 31) { procs[si].name[j] = name[j]; j++; }
        procs[si].name[j] = 0;
    } else {
        procs[si].name[0] = 0;
    }

    proc_next_id++;
    pid_t pid = procs[si].pid;
    mutex_unlock(&proc_mutex);
    return pid;
}

pid_t proc_fork(void) {
    return -1;
}

int proc_exec(const char *path, char **argv) {
    (void)argv;
    u32 entry, stack_top;
    if (elf_load(path, &entry, &stack_top) < 0) return -1;

    process_t *p = proc_get(current_pid);
    if (!p) return -1;

    p->state = PROC_RUNNING;

    __asm__ volatile("cli");

    u32 user_esp = stack_top - 8;
    *(u32*)user_esp = 0;

    __asm__ volatile(
        "mov %0, %%edi\n"
        "mov %1, %%esi\n"
        "pushl $0x23\n"
        "pushl %%esi\n"
        "pushl $0x202\n"
        "pushl $0x1B\n"
        "pushl %%edi\n"
        "iret"
        :
        : "r"(entry), "r"(user_esp)
        : "edi", "esi", "memory"
    );

    return 0;
}

void proc_exit(int code) {
    process_t *p = proc_get(current_pid);
    if (!p) return;
    p->state = PROC_DEAD;
    p->exit_code = code;
    for (int f = 0; f < MAX_FDS; f++) {
        if (p->fds[f].used) {
            file_t *file = (file_t*)p->fds[f].vfs_node;
            if (file) { vfs_close(file); kfree(file); }
            p->fds[f].used = 0;
        }
    }
    while (1) proc_yield();
}

pid_t proc_waitpid(pid_t pid, int *status) {
    return -1;
}

void proc_yield(void) {
    __asm__ volatile("int $0x20");
}

void proc_schedule(void) {
    if (!proc_initialized || live_count() < 1) return;
    process_t *cur = proc_get(current_pid);
    if (!cur) return;
    check_canary(cur);
    if (fpu_initialized)
        __asm__ volatile("fxsave %0" : "=m"(cur->fpu_state));
    cur->saved_esp = task_saved_esp;
    if (cur->state != PROC_DEAD) cur->state = PROC_READY;
    int next = next_task();
    check_canary(&procs[next]);
    current_pid = procs[next].pid;
    procs[next].state = PROC_RUNNING;
    if (fpu_initialized)
        __asm__ volatile("fxrstor %0" : : "m"(procs[next].fpu_state));
    task_new_esp = procs[next].saved_esp;
    task_switch_pending = 1;
}

pid_t proc_getpid(void) {
    return current_pid;
}

pid_t proc_getppid(void) {
    process_t *p = proc_get(current_pid);
    return p ? p->ppid : 0;
}

uid_t proc_getuid(void) {
    process_t *p = proc_get(current_pid);
    return p ? p->uid : 0;
}

int proc_setuid(uid_t uid) {
    process_t *p = proc_get(current_pid);
    if (!p) return -1;
    p->uid = uid;
    return 0;
}

process_t *proc_get(pid_t pid) {
    for (int i = 0; i < MAX_PROC; i++)
        if (procs[i].pid == pid && procs[i].state != PROC_FREE) return &procs[i];
    return 0;
}

int proc_count(void) {
    return live_count();
}

void proc_set_name(const char *name) {
    process_t *p = proc_get(current_pid);
    if (!p || !name) return;
    int j = 0;
    while (name[j] && j < 31) { p->name[j] = name[j]; j++; }
    p->name[j] = 0;
}

int proc_fd_alloc(void) {
    process_t *p = proc_get(current_pid);
    if (!p) return -1;
    for (int i = 3; i < MAX_FDS; i++) {
        if (!p->fds[i].used) {
            p->fds[i].used = 1;
            p->fds[i].refcount = 1;
            p->fds[i].pos = 0;
            p->fds[i].vfs_node = 0;
            return i;
        }
    }
    return -1;
}

void proc_fd_free(int fd) {
    process_t *p = proc_get(current_pid);
    if (!p || fd < 0 || fd >= MAX_FDS) return;
    p->fds[fd].used = 0;
    p->fds[fd].vfs_node = 0;
}

fd_entry_t *proc_fd_get(int fd) {
    process_t *p = proc_get(current_pid);
    if (!p || fd < 0 || fd >= MAX_FDS || !p->fds[fd].used) return 0;
    return &p->fds[fd];
}

static int task_to_pid[MAX_TASKS];
static int task_to_pid_init;

int current_task_id(void) {
    if (!task_to_pid_init) return current_pid % MAX_TASKS;
    for (int i = 0; i < MAX_TASKS; i++)
        if (task_to_pid[i] == current_pid) return i;
    return current_pid % MAX_TASKS;
}

int task_is_initialized(void) { return proc_initialized; }
int task_count(void) { return live_count(); }
void task_init(void) { proc_init(); }
int task_create(task_fn_t func) { return (int)proc_spawn(func, "task"); }
void task_yield(void) { proc_yield(); }
void task_exit(void) { proc_exit(0); }
void task_schedule(void) { proc_schedule(); }
