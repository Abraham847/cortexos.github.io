#ifndef VFS_H
#define VFS_H

#include "core.h"

#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR   2
#define O_CREAT  4
#define O_TRUNC  8

#define S_IFMT   0xF000
#define S_IFDIR  0x4000
#define S_IFREG  0x8000
#define S_IRUSR  0400
#define S_IWUSR  0200
#define S_IXUSR  0100
#define S_IRGRP  0040
#define S_IWGRP  0020
#define S_IXGRP  0010
#define S_IROTH  0004
#define S_IWOTH  0002
#define S_IXOTH  0001
#define S_IRWXU  0700
#define S_IRWXG  0070
#define S_IRWXO  0007

typedef struct {
    u32 size;
    u16 mode;
    u16 uid;
    u16 gid;
    u8  attr;
} stat_t;

typedef struct {
    char name[32];
    u32  ino;
    u8   type;
} dirent_t;

typedef struct file {
    u32 pos;
    u16 mode;
    u16 flags;
    void *private_data;
    stat_t stat;
    int (*read)(struct file *f, u8 *buf, u32 count);
    int (*write)(struct file *f, const u8 *buf, u32 count);
    int (*readdir)(struct file *f, dirent_t *d, int count);
    int (*fstat)(struct file *f, stat_t *s);
    int (*ioctl)(struct file *f, int cmd, void *arg);
    int (*close)(struct file *f);
} file_t;

typedef struct vfs_ops {
    int (*open)(const char *path, int flags, int mode, file_t *file);
    int (*unlink)(const char *path);
    int (*mkdir)(const char *path, int mode);
} vfs_ops_t;

typedef struct mount {
    const char *path;
    int path_len;
    vfs_ops_t *ops;
    void *private_data;
    struct mount *next;
} mount_t;

void vfs_init(void);
int vfs_mount(const char *path, vfs_ops_t *ops, void *private_data);
int vfs_open(const char *path, int flags, int mode, file_t *file);
int vfs_close(file_t *file);
int vfs_read(file_t *file, u8 *buf, u32 count);
int vfs_write(file_t *file, const u8 *buf, u32 count);
int vfs_readdir(file_t *file, dirent_t *d, int count);
int vfs_stat(file_t *file, stat_t *s);
int vfs_ioctl(file_t *file, int cmd, void *arg);
int vfs_unlink(const char *path);
int vfs_mkdir(const char *path, int mode);
mount_t *vfs_find_mount(const char *path);

#endif
