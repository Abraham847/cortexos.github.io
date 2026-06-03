#include "vfs.h"
#include "synch.h"
#include "kb.h"
#include "mouse.h"

#define DEVFS_MAX 8

static int devfs_read(file_t *f, u8 *buf, u32 count);
static int devfs_write(file_t *f, const u8 *buf, u32 count);
static int devfs_readdir(file_t *f, dirent_t *d, int count);
static int devfs_stat(file_t *f, stat_t *s);
static int devfs_close(file_t *f);

typedef struct {
    char name[16];
    int (*read_fn)(u8 *buf, u32 count);
    int (*write_fn)(const u8 *buf, u32 count);
    int (*ioctl_fn)(int cmd, void *arg);
    int type;
} devfs_entry_t;

static devfs_entry_t devfs_devs[DEVFS_MAX];
static int devfs_count;

int devfs_register(const char *name,
                   int (*read)(u8*, u32),
                   int (*write)(const u8*, u32),
                   int (*ioctl)(int, void*),
                   int type) {
    if (devfs_count >= DEVFS_MAX) return -1;
    int i = devfs_count++;
    int j = 0;
    while (name[j] && j < 15) { devfs_devs[i].name[j] = name[j]; j++; }
    devfs_devs[i].name[j] = 0;
    devfs_devs[i].read_fn = read;
    devfs_devs[i].write_fn = write;
    devfs_devs[i].ioctl_fn = ioctl;
    devfs_devs[i].type = type;
    return 0;
}

static int devfs_null_read(u8 *buf, u32 count) { return 0; }
static int devfs_null_write(const u8 *buf, u32 count) { return count; }
static int devfs_zero_read(u8 *buf, u32 count) { memset(buf, 0, count); return count; }

static int devfs_fb_read(u8 *buf, u32 count) {
    extern u32 vga_fb_addr;
    u32 fb = vga_fb_addr;
    memcpy(buf, (void*)fb, count);
    return count;
}

static int devfs_fb_write(const u8 *buf, u32 count) {
    extern u32 vga_fb_addr;
    memcpy((void*)vga_fb_addr, buf, count);
    return count;
}

static int devfs_fb_ioctl(int cmd, void *arg) {
    extern u16 vga_width, vga_height, vga_pitch;
    if (cmd == 0) {
        u16 *res = (u16*)arg;
        res[0] = vga_width; res[1] = vga_height; res[2] = vga_pitch;
    }
    return 0;
}

static int devfs_kbd_read(u8 *buf, u32 count) {
    if (count < 1) return 0;
    *buf = (u8)kb_getchar();
    return 1;
}

static int devfs_kbd_write(const u8 *buf, u32 count) { return count; }

static int devfs_mouse_read(u8 *buf, u32 count) {
    extern volatile int mouse_x, mouse_y, mouse_btn;
    if (count < 12) return 0;
    __asm__ volatile("cli");
    memcpy(buf, (void*)&mouse_x, 4);
    memcpy(buf + 4, (void*)&mouse_y, 4);
    memcpy(buf + 8, (void*)&mouse_btn, 4);
    __asm__ volatile("sti");
    return 12;
}

int devfs_open(const char *path, int flags, int mode, file_t *file) {
    if (path[0] == 0 || strcmp(path, ".") == 0) {
        file->private_data = 0;
        file->pos = 0;
        file->read = 0;
        file->write = 0;
        file->readdir = devfs_readdir;
        file->fstat = devfs_stat;
        file->close = devfs_close;
        stat_t s; s.size = 0; s.mode = S_IFDIR | S_IRUSR; s.uid = 0; s.gid = 0;
        memcpy(&file->stat, &s, sizeof(stat_t));
        return 0;
    }

    for (int i = 0; i < devfs_count; i++) {
        if (strcmp(path, devfs_devs[i].name) == 0) {
            file->private_data = (void*)(i + 1);
            file->pos = 0;
            file->read = devfs_read;
            file->write = devfs_write;
            file->readdir = 0;
            file->fstat = devfs_stat;
            file->close = devfs_close;
            stat_t s;
            s.size = 0;
            s.mode = S_IFREG | S_IRUSR | S_IWUSR;
            s.uid = 0; s.gid = 0;
            memcpy(&file->stat, &s, sizeof(stat_t));
            return 0;
        }
    }
    return -1;
}

static int devfs_read(file_t *file, u8 *buf, u32 count) {
    int idx = (int)file->private_data - 1;
    if (idx < 0 || idx >= devfs_count) return -1;
    if (devfs_devs[idx].read_fn) return devfs_devs[idx].read_fn(buf, count);
    return 0;
}

static int devfs_write(file_t *file, const u8 *buf, u32 count) {
    int idx = (int)file->private_data - 1;
    if (idx < 0 || idx >= devfs_count) return -1;
    if (devfs_devs[idx].write_fn) return devfs_devs[idx].write_fn(buf, count);
    return count;
}

static int devfs_ioctl(file_t *file, int cmd, void *arg) {
    int idx = (int)file->private_data - 1;
    if (idx < 0 || idx >= devfs_count) return -1;
    if (devfs_devs[idx].ioctl_fn) return devfs_devs[idx].ioctl_fn(cmd, arg);
    return 0;
}

static int devfs_readdir(file_t *file, dirent_t *d, int count) {
    if (count <= 0) return 0;
    int pos = file->pos;
    int written = 0;

    if (pos == 0 && written < count) {
        d[written].ino = 0; d[written].type = 2;
        d[written].name[0] = '.'; d[written].name[1] = 0;
        written++; file->pos++;
    }
    if (pos == 1 && written < count) {
        d[written].ino = 1; d[written].type = 2;
        d[written].name[0] = '.'; d[written].name[1] = '.'; d[written].name[2] = 0;
        written++; file->pos++;
    }

    int di = pos - 2;
    while (di < devfs_count && written < count) {
        int j = 0;
        while (devfs_devs[di].name[j] && j < 15) {
            d[written].name[j] = devfs_devs[di].name[j]; j++;
        }
        d[written].name[j] = 0;
        d[written].ino = 2 + di;
        d[written].type = 1;
        written++; file->pos++; di++;
    }
    return written;
}

static int devfs_stat(file_t *file, stat_t *s) {
    memcpy(s, &file->stat, sizeof(stat_t));
    return 0;
}

static int devfs_close(file_t *file) {
    return 0;
}

static vfs_ops_t devfs_ops = {
    devfs_open, 0, 0
};

void devfs_init(void) {
    devfs_count = 0;

    devfs_register("null", devfs_null_read, devfs_null_write, 0, 0);
    devfs_register("zero", devfs_zero_read, devfs_null_write, 0, 0);
    devfs_register("fb", devfs_fb_read, devfs_fb_write, devfs_fb_ioctl, 1);
    devfs_register("kbd", devfs_kbd_read, devfs_kbd_write, 0, 2);
    devfs_register("mouse", devfs_mouse_read, 0, 0, 3);

    vfs_mount("/dev", &devfs_ops, 0);
}
