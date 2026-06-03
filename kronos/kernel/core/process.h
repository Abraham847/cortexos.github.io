#ifndef PROCESS_H
#define PROCESS_H

#include "core.h"

#define PID_MAX 4096
#define MAX_FDS 32
#define MAX_PROC 64
#define STACK_CANARY 0xDEADC0DE

typedef enum { PROC_FREE, PROC_READY, PROC_RUNNING, PROC_BLOCKED, PROC_ZOMBIE, PROC_DEAD } proc_state_t;
typedef void (*proc_fn_t)(void);

typedef struct {
    int used;
    int flags;
    u32 pos;
    void *vfs_node;
    int refcount;
} fd_entry_t;

typedef struct {
    u32 handler;
    int pending;
} signal_entry_t;

typedef struct {
    pid_t pid;
    pid_t ppid;
    uid_t uid;
    uid_t gid;
    char name[32];
    proc_state_t state;
    int exit_code;

    int priority;
    int quantum;
    u32 saved_esp;
    u32 stack[4096];
    proc_fn_t func;
    u32 page_dir;

    fd_entry_t fds[MAX_FDS];
    signal_entry_t signals[32];
    u8 fpu_state[512] __attribute__((aligned(16)));
} process_t;

void proc_init(void);
pid_t proc_spawn(proc_fn_t func, const char *name);
pid_t proc_fork(void);
int proc_exec(const char *path, char **argv);
void proc_exit(int code);
pid_t proc_waitpid(pid_t pid, int *status);
void proc_yield(void);
void proc_schedule(void);
pid_t proc_getpid(void);
pid_t proc_getppid(void);
uid_t proc_getuid(void);
int proc_setuid(uid_t uid);
process_t *proc_get(pid_t pid);
int proc_count(void);
void proc_set_name(const char *name);
int proc_fd_alloc(void);
void proc_fd_free(int fd);
fd_entry_t *proc_fd_get(int fd);

#endif
