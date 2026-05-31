#ifndef FS_H
#define FS_H

#include "kernel.h"

#define FS_ATTR_DIR    0x10
#define FS_ATTR_HIDDEN 0x02
#define FS_ATTR_SYSTEM 0x04
#define FS_ATTR_LABEL  0x08

typedef struct { char name[13]; u8 attr; u16 cluster; u32 size; } fs_entry_t;

int fs_init(void);
int fs_list(const char *path, fs_entry_t *entries, int max);
int fs_read(const char *path, u8 *buf, u32 max_size);
int fs_write(const char *path, const u8 *buf, u32 size);
int fs_delete(const char *path);
int fs_rename(const char *oldp, const char *newp);

#endif
