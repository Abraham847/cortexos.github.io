#include "syscall.h"
#include "vfs.h"
#include "process.h"
#include "task.h"
#include "heap.h"
#include "vga.h"
#include "kb.h"
#include "timer.h"

typedef u32 (*syscall_fn)(u32, u32, u32, u32, u32);

static syscall_fn syscall_table[SYS_MAX];

static u32 sys_exit(u32 code, u32 a2, u32 a3, u32 a4, u32 a5) {
    proc_exit((int)code);
    return 0;
}

static u32 sys_fork(u32 a1, u32 a2, u32 a3, u32 a4, u32 a5) {
    return (u32)proc_fork();
}

static u32 sys_read(u32 fd, u32 buf, u32 count, u32 a4, u32 a5) {
    fd_entry_t *fde = proc_fd_get((int)fd);
    if (!fde) return -1;
    return (u32)vfs_read((file_t*)fde->vfs_node, (u8*)buf, count);
}

static u32 sys_write(u32 fd, u32 buf, u32 count, u32 a4, u32 a5) {
    fd_entry_t *fde = proc_fd_get((int)fd);
    if (!fde) return -1;
    return (u32)vfs_write((file_t*)fde->vfs_node, (const u8*)buf, count);
}

static u32 sys_open(u32 path, u32 flags, u32 mode, u32 a4, u32 a5) {
    file_t *f = (file_t*)kmalloc(sizeof(file_t));
    if (!f) return -1;
    if (vfs_open((const char*)path, (int)flags, (int)mode, f) < 0) {
        kfree(f); return -1;
    }
    int fd = proc_fd_alloc();
    if (fd < 0) { vfs_close(f); kfree(f); return -1; }
    fd_entry_t *fde = proc_fd_get(fd);
    fde->vfs_node = (void*)f;
    fde->flags = (int)flags;
    return (u32)fd;
}

static u32 sys_close(u32 fd, u32 a2, u32 a3, u32 a4, u32 a5) {
    fd_entry_t *fde = proc_fd_get((int)fd);
    if (!fde) return -1;
    file_t *f = (file_t*)fde->vfs_node;
    if (f) { vfs_close(f); kfree(f); }
    proc_fd_free((int)fd);
    return 0;
}

static u32 sys_getpid(u32 a1, u32 a2, u32 a3, u32 a4, u32 a5) {
    return (u32)proc_getpid();
}

static u32 sys_getppid(u32 a1, u32 a2, u32 a3, u32 a4, u32 a5) {
    return (u32)proc_getppid();
}

static u32 sys_kill(u32 pid, u32 sig, u32 a3, u32 a4, u32 a5) {
    process_t *p = proc_get((pid_t)pid);
    if (!p) return -1;
    if ((int)sig < 32) p->signals[(int)sig].pending = 1;
    return 0;
}

static u32 sys_brk(u32 addr, u32 a2, u32 a3, u32 a4, u32 a5) {
    extern u32 heap_brk;
    if (addr == 0) return heap_brk;
    if (addr > 0x80000) return -1;
    heap_brk = addr;
    return heap_brk;
}

static u32 sys_execve(u32 path, u32 argv, u32 a3, u32 a4, u32 a5) {
    return (u32)proc_exec((const char*)path, (char**)argv);
}

static u32 sys_waitpid(u32 pid, u32 status, u32 opts, u32 a4, u32 a5) {
    return (u32)proc_waitpid((pid_t)pid, (int*)status);
}

static u32 sys_signal(u32 sig, u32 handler, u32 a3, u32 a4, u32 a5) {
    process_t *p = proc_get(proc_getpid());
    if (!p || (int)sig >= 32) return -1;
    u32 old = p->signals[(int)sig].handler;
    p->signals[(int)sig].handler = handler;
    return old;
}

static u32 sys_ioctl(u32 fd, u32 cmd, u32 arg, u32 a4, u32 a5) {
    fd_entry_t *fde = proc_fd_get((int)fd);
    if (!fde) return -1;
    return (u32)vfs_ioctl((file_t*)fde->vfs_node, (int)cmd, (void*)arg);
}

static u32 sys_getdents(u32 fd, u32 buf, u32 count, u32 a4, u32 a5) {
    fd_entry_t *fde = proc_fd_get((int)fd);
    if (!fde) return -1;
    return (u32)vfs_readdir((file_t*)fde->vfs_node, (dirent_t*)buf, (int)count);
}

static u32 sys_chdir(u32 path, u32 a2, u32 a3, u32 a4, u32 a5) {
    return 0;
}

static u32 sys_stat(u32 path, u32 buf, u32 a3, u32 a4, u32 a5) {
    file_t f;
    if (vfs_open((const char*)path, O_RDONLY, 0, &f) < 0) return -1;
    stat_t s;
    int r = vfs_stat(&f, &s);
    if (r == 0 && buf) memcpy((void*)buf, &s, sizeof(stat_t));
    if (f.close) f.close(&f);
    return r;
}

static u32 sys_dup(u32 fd, u32 a2, u32 a3, u32 a4, u32 a5) {
    fd_entry_t *fde = proc_fd_get((int)fd);
    if (!fde) return -1;
    int nfd = proc_fd_alloc();
    if (nfd < 0) return -1;
    fd_entry_t *nfde = proc_fd_get(nfd);
    memcpy(nfde, fde, sizeof(fd_entry_t));
    nfde->refcount++;
    return (u32)nfd;
}

static u32 sys_time(u32 a1, u32 a2, u32 a3, u32 a4, u32 a5) {
    return timer_ticks;
}

static u32 sys_reboot(u32 cmd, u32 a2, u32 a3, u32 a4, u32 a5) {
    if (cmd == 0x1234) {
        __asm__ volatile("cli; mov $0x1234, %ax; mov $0, %bx; int $0x19");
    }
    return 0;
}

static u32 sys_mkdir(u32 path, u32 mode, u32 a3, u32 a4, u32 a5) {
    return (u32)vfs_mkdir((const char*)path, (int)mode);
}

static u32 sys_unlink(u32 path, u32 a2, u32 a3, u32 a4, u32 a5) {
    return (u32)vfs_unlink((const char*)path);
}

static u32 sys_chmod(u32 path, u32 mode, u32 a3, u32 a4, u32 a5) {
    return 0;
}

static u32 sys_getuid(u32 a1, u32 a2, u32 a3, u32 a4, u32 a5) {
    return (u32)proc_getuid();
}

static u32 sys_setuid(u32 uid, u32 a2, u32 a3, u32 a4, u32 a5) {
    return (u32)proc_setuid((uid_t)uid);
}

static u32 sys_getgid(u32 a1, u32 a2, u32 a3, u32 a4, u32 a5) {
    process_t *p = proc_get(proc_getpid());
    return p ? p->gid : 0;
}

static u32 sys_setgid(u32 gid, u32 a2, u32 a3, u32 a4, u32 a5) {
    process_t *p = proc_get(proc_getpid());
    if (p) p->gid = (uid_t)gid;
    return 0;
}

static u32 sys_yield(u32 a1, u32 a2, u32 a3, u32 a4, u32 a5) {
    proc_yield();
    return 0;
}

void syscall_init(void) {
    for (int i = 0; i < SYS_MAX; i++) syscall_table[i] = 0;
    syscall_table[SYS_exit]    = sys_exit;
    syscall_table[SYS_fork]    = sys_fork;
    syscall_table[SYS_read]    = sys_read;
    syscall_table[SYS_write]   = sys_write;
    syscall_table[SYS_open]    = sys_open;
    syscall_table[SYS_close]   = sys_close;
    syscall_table[SYS_getpid]  = sys_getpid;
    syscall_table[SYS_kill]    = sys_kill;
    syscall_table[SYS_brk]     = sys_brk;
    syscall_table[SYS_execve]  = sys_execve;
    syscall_table[SYS_waitpid] = sys_waitpid;
    syscall_table[SYS_signal]  = sys_signal;
    syscall_table[SYS_ioctl]   = sys_ioctl;
    syscall_table[SYS_getdents] = sys_getdents;
    syscall_table[SYS_chdir]   = sys_chdir;
    syscall_table[SYS_stat]    = sys_stat;
    syscall_table[SYS_dup]     = sys_dup;
    syscall_table[SYS_time]    = sys_time;
    syscall_table[SYS_reboot]  = sys_reboot;
    syscall_table[SYS_mkdir]   = sys_mkdir;
    syscall_table[SYS_unlink]  = sys_unlink;
    syscall_table[SYS_chmod]   = sys_chmod;
    syscall_table[SYS_getuid]  = sys_getuid;
    syscall_table[SYS_setuid]  = sys_setuid;
    syscall_table[SYS_getgid]  = sys_getgid;
    syscall_table[SYS_setgid]  = sys_setgid;
    syscall_table[SYS_getppid] = sys_getppid;
    syscall_table[SYS_yield]   = sys_yield;
}

u32 syscall_handler(u32 nr, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5) {
    if (nr >= SYS_MAX || !syscall_table[nr]) return -1;
    return syscall_table[nr](a1, a2, a3, a4, a5);
}
