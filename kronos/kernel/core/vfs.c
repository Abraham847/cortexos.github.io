#include "vfs.h"
#include "synch.h"
#include "heap.h"

static int strncmp(const char *a, const char *b, int n) {
    for (int i = 0; i < n; i++) {
        if (a[i] != b[i]) return a[i] - b[i];
        if (a[i] == 0) return 0;
    }
    return 0;
}

static mount_t *mounts;
static mutex_t vfs_mutex = MUTEX_INIT;

void vfs_init(void) {
    mounts = 0;
}

int vfs_mount(const char *path, vfs_ops_t *ops, void *private_data) {
    mutex_lock(&vfs_mutex);
    mount_t *m = (mount_t*)kmalloc(sizeof(mount_t));
    if (!m) { mutex_unlock(&vfs_mutex); return -1; }
    m->path = path;
    m->path_len = strlen(path);
    m->ops = ops;
    m->private_data = private_data;
    m->next = mounts;
    mounts = m;
    mutex_unlock(&vfs_mutex);
    return 0;
}

mount_t *vfs_find_mount(const char *path) {
    mount_t *best = 0;
    int best_len = 0;
    mount_t *m = mounts;
    while (m) {
        int plen = m->path_len;
        if (plen == 1 && m->path[0] == '/') { m = m->next; continue; }
        if (strncmp(path, m->path, plen) == 0) {
            if (path[plen] == '/' || path[plen] == 0) {
                if (plen > best_len) { best = m; best_len = plen; }
            }
        }
        m = m->next;
    }
    if (!best) {
        m = mounts;
        while (m) {
            if (m->path_len == 1 && m->path[0] == '/') { best = m; break; }
            m = m->next;
        }
    }
    return best;
}

static const char *vfs_strip_mount(const char *path, mount_t *mnt) {
    if (!mnt || mnt->path_len <= 1) return path;
    return path + mnt->path_len;
}

int vfs_open(const char *path, int flags, int mode, file_t *file) {
    mount_t *mnt = vfs_find_mount(path);
    if (!mnt || !mnt->ops || !mnt->ops->open) return -1;
    const char *rel = vfs_strip_mount(path, mnt);
    if (rel[0] == '/') rel++;
    return mnt->ops->open(rel, flags, mode, file);
}

int vfs_close(file_t *file) {
    if (!file || !file->close) return -1;
    return file->close(file);
}

int vfs_read(file_t *file, u8 *buf, u32 count) {
    if (!file || !file->read) return -1;
    return file->read(file, buf, count);
}

int vfs_write(file_t *file, const u8 *buf, u32 count) {
    if (!file || !file->write) return -1;
    return file->write(file, buf, count);
}

int vfs_readdir(file_t *file, dirent_t *d, int count) {
    if (!file || !file->readdir) return -1;
    return file->readdir(file, d, count);
}

int vfs_stat(file_t *file, stat_t *s) {
    if (!file || !file->fstat) return -1;
    return file->fstat(file, s);
}

int vfs_ioctl(file_t *file, int cmd, void *arg) {
    if (!file || !file->ioctl) return -1;
    return file->ioctl(file, cmd, arg);
}

int vfs_unlink(const char *path) {
    mount_t *mnt = vfs_find_mount(path);
    if (!mnt || !mnt->ops || !mnt->ops->unlink) return -1;
    const char *rel = vfs_strip_mount(path, mnt);
    if (rel[0] == '/') rel++;
    return mnt->ops->unlink(rel);
}

int vfs_mkdir(const char *path, int mode) {
    mount_t *mnt = vfs_find_mount(path);
    if (!mnt || !mnt->ops || !mnt->ops->mkdir) return -1;
    const char *rel = vfs_strip_mount(path, mnt);
    if (rel[0] == '/') rel++;
    return mnt->ops->mkdir(rel, mode);
}
