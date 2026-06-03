#include "vfs.h"
#include "process.h"
#include "task.h"
#include "synch.h"

extern u16 vga_width, vga_height;

static int procfs_read(file_t *f, u8 *buf, u32 count);
static int procfs_readdir(file_t *f, dirent_t *d, int count);
static int procfs_stat(file_t *f, stat_t *s);

typedef struct {
    char name[32];
    char content[256];
    int content_len;
    int is_dir;
} procfs_node_t;

#define PROCFS_NODES 8
static procfs_node_t procfs_nodes[PROCFS_NODES];
static int procfs_node_count;

static int procfs_add(const char *name, int is_dir) {
    if (procfs_node_count >= PROCFS_NODES) return -1;
    int i = procfs_node_count++;
    int j = 0;
    while (name[j] && j < 31) { procfs_nodes[i].name[j] = name[j]; j++; }
    procfs_nodes[i].name[j] = 0;
    procfs_nodes[i].content_len = 0;
    procfs_nodes[i].is_dir = is_dir;
    return i;
}

static void procfs_set(int idx, const char *s) {
    int j = 0;
    while (s[j] && j < 255) { procfs_nodes[idx].content[j] = s[j]; j++; }
    procfs_nodes[idx].content[j] = 0;
    procfs_nodes[idx].content_len = j;
}

int procfs_open(const char *path, int flags, int mode, file_t *file) {
    if (flags != O_RDONLY) return -1;
    if (path[0] == 0 || strcmp(path, ".") == 0) {
        file->private_data = (void*)1;
        file->pos = 0;
        file->read = 0;
        file->write = 0;
        file->readdir = procfs_readdir;
        file->fstat = procfs_stat;
        file->ioctl = 0;
        file->close = 0;
        stat_t s;
        s.size = 0; s.mode = S_IFDIR | S_IRUSR; s.uid = 0; s.gid = 0;
        memcpy(&file->stat, &s, sizeof(stat_t));
        return 0;
    }
    for (int i = 0; i < procfs_node_count; i++) {
        if (strcmp(path, procfs_nodes[i].name) == 0) {
            file->private_data = (void*)(i + 1);
            file->pos = 0;
            file->read = procfs_read;
            file->write = 0;
            file->readdir = 0;
            file->fstat = procfs_stat;
            file->ioctl = 0;
            file->close = 0;
            stat_t s;
            s.size = procfs_nodes[i].content_len;
            s.mode = S_IFREG | S_IRUSR | S_IRGRP | S_IROTH;
            s.uid = 0; s.gid = 0;
            memcpy(&file->stat, &s, sizeof(stat_t));
            return 0;
        }
    }
    return -1;
}

static int procfs_read(file_t *file, u8 *buf, u32 count) {
    int idx = (int)file->private_data - 1;
    if (idx < 0 || idx >= procfs_node_count) return -1;
    int remain = procfs_nodes[idx].content_len - file->pos;
    if (remain <= 0) return 0;
    if (count > (u32)remain) count = remain;
    memcpy(buf, procfs_nodes[idx].content + file->pos, count);
    file->pos += count;
    return count;
}

static int procfs_readdir(file_t *file, dirent_t *d, int count) {
    if (count <= 0) return 0;
    int pos = file->pos;
    int written = 0;

    if (pos == 0) {
        d[written].ino = 0; d[written].type = 2;
        d[written].name[0] = '.'; d[written].name[1] = 0;
        written++; file->pos++;
    }

    if (pos == 1 && written < count) {
        d[written].ino = 1; d[written].type = 2;
        d[written].name[0] = '.'; d[written].name[1] = '.'; d[written].name[2] = 0;
        written++; file->pos++;
    }

    int node_idx = pos - 2;
    while (node_idx < procfs_node_count && written < count) {
        int j = 0;
        while (procfs_nodes[node_idx].name[j] && j < 31) {
            d[written].name[j] = procfs_nodes[node_idx].name[j]; j++;
        }
        d[written].name[j] = 0;
        d[written].ino = 2 + node_idx;
        d[written].type = procfs_nodes[node_idx].is_dir ? 2 : 1;
        written++; file->pos++; node_idx++;
    }

    return written;
}

static int procfs_stat(file_t *file, stat_t *s) {
    memcpy(s, &file->stat, sizeof(stat_t));
    return 0;
}

static vfs_ops_t procfs_ops = {
    procfs_open, 0, 0
};

void procfs_init(void) {
    procfs_node_count = 0;
    procfs_add("meminfo", 0);
    procfs_add("uptime", 0);
    procfs_add("tasks", 0);
    procfs_add("version", 0);
    procfs_add("cpuinfo", 0);
    vfs_mount("/proc", &procfs_ops, 0);
}

void procfs_update(void) {
    char buf[64];
    itoa(64 * 1024, buf); procfs_set(0, buf);
    itoa(timer_ticks / 100, buf); procfs_set(1, buf);
    itoa(proc_count(), buf); procfs_set(2, buf);
    procfs_set(3, "CortexOS v2.0");
    procfs_set(4, "i686");
}
