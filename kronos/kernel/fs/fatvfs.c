#include "vfs.h"
#include "fs.h"
#include "heap.h"
#include "synch.h"

extern mutex_t mutex_fs;

typedef struct {
    char name[13];
    u32 size;
    u32 pos;
} fat12_data_t;

typedef struct {
    char *current_path;
} fat12_private_t;

static fat12_private_t fat12_data;

static int fat12_read(file_t *f, u8 *buf, u32 count);
static int fat12_write(file_t *f, const u8 *buf, u32 count);
static int fat12_readdir(file_t *f, dirent_t *d, int count);
static int fat12_stat(file_t *f, stat_t *s);
static int fat12_close(file_t *f);

static int fat12_build_path(const char *rel, char *out, int max) {
    if (rel[0] == 0 || rel[0] == '/') {
        out[0] = '.';
        out[1] = 0;
        return 0;
    }
    int j = 0;
    while (rel[j] && j < max - 1) {
        int k = 0;
        char name[13];
        while (rel[j] && rel[j] != '/' && k < 12) { name[k++] = rel[j++]; }
        name[k] = 0;
        if (k == 0) { if (rel[j] == '/') j++; continue; }
        int pos = 0;
        while (name[pos] && pos < 12) {
            if (name[pos] >= 'a' && name[pos] <= 'z') out[pos] = name[pos] - 32;
            else out[pos] = name[pos];
            pos++;
        }
        out[pos] = 0;
        if (rel[j] == '/') j++;
        return 0;
    }
    out[0] = '.';
    out[1] = 0;
    return 0;
}

int fat12_open(const char *path, int flags, int mode, file_t *file) {
    char name[13];
    fat12_build_path(path, name, 13);

    fs_entry_t entries[224];
    int n = fs_list("", entries, 224);
    int found = -1;
    for (int i = 0; i < n; i++) {
        if (strcmp(entries[i].name, name) == 0 || strcmp(name, ".") == 0) {
            found = i; break;
        }
    }

    if (found < 0 && (flags & O_CREAT)) {
        if (fs_write(name, (const u8*)"", 0) == 0) {
            found = n;
            entries[found].size = 0;
        }
    }

    if (found < 0) return -1;

    fat12_data_t *priv = (fat12_data_t*)kmalloc(sizeof(fat12_data_t));
    if (!priv) return -1;
    priv->name[0] = 0;
    int k = 0;
    while (name[k] && k < 12) { priv->name[k] = name[k]; k++; }
    priv->name[k] = 0;
    priv->size = entries[found].size;
    priv->pos = 0;
    if (flags & O_TRUNC) {
        fs_delete(name);
        fs_write(name, (const u8*)"", 0);
        priv->size = 0;
    }

    file->private_data = (void*)priv;
    file->pos = 0;
    file->flags = flags;
    file->read = fat12_read;
    file->write = fat12_write;
    file->readdir = fat12_readdir;
    file->fstat = fat12_stat;
    file->close = fat12_close;

    stat_t s;
    s.size = priv->size;
    s.mode = S_IFREG | S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
    s.uid = 0; s.gid = 0;
    memcpy(&file->stat, &s, sizeof(stat_t));
    return 0;
}

static int fat12_read(file_t *file, u8 *buf, u32 count) {
    fat12_data_t *priv = (fat12_data_t*)file->private_data;
    if (!priv) return -1;

    u32 remain = priv->size - file->pos;
    if (remain <= 0) return 0;
    if (count > remain) count = remain;

    u8 *tmp = (u8*)kmalloc(priv->size + 1);
    if (!tmp) return -1;
    mutex_lock(&mutex_fs);
    int r = fs_read(priv->name, tmp, priv->size);
    mutex_unlock(&mutex_fs);
    if (r < 0) { kfree(tmp); return -1; }
    memcpy(buf, tmp + file->pos, count);
    file->pos += count;
    kfree(tmp);
    return count;
}

static int fat12_write(file_t *file, const u8 *buf, u32 count) {
    fat12_data_t *priv = (fat12_data_t*)file->private_data;
    if (!priv) return -1;

    u32 new_size = file->pos + count;
    u8 *tmp = 0;

    if (priv->size > 0) {
        tmp = (u8*)kmalloc(new_size + 1);
        if (!tmp) return -1;
        mutex_lock(&mutex_fs);
        fs_read(priv->name, tmp, priv->size);
        mutex_unlock(&mutex_fs);
    } else {
        tmp = (u8*)kmalloc(new_size + 1);
        if (!tmp) return -1;
    }

    memcpy(tmp + file->pos, buf, count);

    mutex_lock(&mutex_fs);
    fs_write(priv->name, tmp, new_size);
    mutex_unlock(&mutex_fs);

    priv->size = new_size;
    file->pos += count;
    stat_t s;
    s.size = priv->size; s.mode = S_IFREG | S_IRUSR | S_IWUSR; s.uid = 0; s.gid = 0;
    memcpy(&file->stat, &s, sizeof(stat_t));

    kfree(tmp);
    return count;
}

static int fat12_readdir(file_t *file, dirent_t *d, int count) {
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

    fs_entry_t entries[224];
    int n = fs_list("", entries, 224);
    int fi = pos - 2;

    while (fi < n && written < count) {
        int j = 0;
        while (entries[fi].name[j] && j < 31) {
            int c = entries[fi].name[j];
            d[written].name[j] = (c >= 'A' && c <= 'Z') ? (c + 32) : c;
            j++;
        }
        d[written].name[j] = 0;
        d[written].ino = 2 + fi;
        d[written].type = (entries[fi].attr & FS_ATTR_DIR) ? 2 : 1;
        written++; file->pos++; fi++;
    }
    return written;
}

static int fat12_stat(file_t *file, stat_t *s) {
    memcpy(s, &file->stat, sizeof(stat_t));
    return 0;
}

static int fat12_close(file_t *file) {
    if (file->private_data) kfree(file->private_data);
    return 0;
}

static int fat12_unlink(const char *path) {
    char name[13];
    fat12_build_path(path, name, 13);
    mutex_lock(&mutex_fs);
    int r = fs_delete(name);
    mutex_unlock(&mutex_fs);
    return r;
}

static vfs_ops_t fat12_vfs_ops = {
    fat12_open, fat12_unlink, 0
};

void fatvfs_init(void) {
    fat12_data.current_path = "/";
    vfs_mount("/", &fat12_vfs_ops, &fat12_data);
}
