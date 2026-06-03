#ifndef SYSCALL_H
#define SYSCALL_H

#include "core.h"

#define SYS_exit    0
#define SYS_fork    1
#define SYS_read    2
#define SYS_write   3
#define SYS_open    4
#define SYS_close   5
#define SYS_getpid  6
#define SYS_kill    7
#define SYS_brk     8
#define SYS_execve  9
#define SYS_waitpid 10
#define SYS_signal  11
#define SYS_ioctl   12
#define SYS_getdents 13
#define SYS_chdir   14
#define SYS_stat    15
#define SYS_dup     16
#define SYS_pipe    17
#define SYS_time    18
#define SYS_reboot  19
#define SYS_mkdir   20
#define SYS_unlink  21
#define SYS_chmod   22
#define SYS_getuid  23
#define SYS_setuid  24
#define SYS_getgid  25
#define SYS_setgid  26
#define SYS_getppid 27
#define SYS_yield   28
#define SYS_MAX     29

void syscall_init(void);
u32 syscall_handler(u32 nr, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5);

#endif
