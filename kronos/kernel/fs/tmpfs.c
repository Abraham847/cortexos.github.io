#include "vfs.h"
#include "heap.h"
#include "synch.h"

#define TMPFS_MAX 32

static int tmpfs_read(file_t *f, u8 *buf, u32 count);
static int tmpfs_write(file_t *f, const u8 *buf, u32 count);
static int tmpfs_readdir(file_t *f, dirent_t *d, int count);
static int tmpfs_stat(file_t *f, stat_t *s);

typedef struct tmpfs_node {
    char name[32];
    u8 *data;
    u32 size;
    u32 capacity;
    int is_dir;
    struct tmpfs_node *children[TMPFS_MAX];
    int child_count;
} tmpfs_node_t;

static tmpfs_node_t tmpfs_root = { "/", 0, 0, 0, 1, {0}, 0 };
static mutex_t tmpfs_mutex = MUTEX_INIT;

static tmpfs_node_t *tmpfs_find(const char *path) {
    if (path[0] == 0 || strcmp(path, ".") == 0 || strcmp(path, "/") == 0)
        return &tmpfs_root;
    if (path[0] == '/') path++;
    tmpfs_node_t *cur = &tmpfs_root;
    char buf[32];
    int bi = 0;
    while (*path) {
        if (*path == '/') {
            buf[bi] = 0; bi = 0;
            int found = 0;
            for (int i = 0; i < cur->child_count; i++) {
                if (strcmp(cur->children[i]->name, buf) == 0) {
                    cur = cur->children[i]; found = 1; break;
                }
            }
            if (!found) return 0;
        } else {
            if (bi < 31) buf[bi++] = *path;
        }
        path++;
    }
    if (bi > 0) {
        buf[bi] = 0;
        for (int i = 0; i < cur->child_count; i++) {
            if (strcmp(cur->children[i]->name, buf) == 0)
                return cur->children[i];
        }
        return 0;
    }
    return cur;
}

static tmpfs_node_t *tmpfs_create_node(const char *name, int is_dir) {
    tmpfs_node_t *n = (tmpfs_node_t*)kmalloc(sizeof(tmpfs_node_t));
    if (!n) return 0;
    int j = 0;
    while (name[j] && j < 31) { n->name[j] = name[j]; j++; }
    n->name[j] = 0;
    n->data = 0;
    n->size = 0;
    n->capacity = 0;
    n->is_dir = is_dir;
    n->child_count = 0;
    return n;
}

int tmpfs_open(const char *path, int flags, int mode, file_t *file) {
    mutex_lock(&tmpfs_mutex);
    tmpfs_node_t *node = tmpfs_find(path);

    if (!node && (flags & O_CREAT)) {
        char parent_path[128];
        char name[32];
        int li = strlen(path) - 1;
        while (li >= 0 && path[li] != '/') li--;
        if (li < 0) { mutex_unlock(&tmpfs_mutex); return -1; }
        memcpy(parent_path, path, li);
        parent_path[li] = 0;
        if (li == 0) { parent_path[0] = '/'; parent_path[1] = 0; }
        memcpy(name, path + li + 1, strlen(path) - li);
        name[strlen(path) - li - 1] = 0;

        tmpfs_node_t *parent = tmpfs_find(parent_path);
        if (!parent || !parent->is_dir) { mutex_unlock(&tmpfs_mutex); return -1; }

        node = tmpfs_create_node(name, 0);
        if (!node) { mutex_unlock(&tmpfs_mutex); return -1; }
        parent->children[parent->child_count++] = node;
    }

    if (!node) { mutex_unlock(&tmpfs_mutex); return -1; }

    file->private_data = (void*)node;
    file->pos = 0;
    if (flags & O_TRUNC) { node->size = 0; }

    file->read = tmpfs_read;
    file->write = tmpfs_write;
    file->readdir = node->is_dir ? tmpfs_readdir : 0;
    file->fstat = tmpfs_stat;
    file->close = 0;

    stat_t s;
    s.size = node->size;
    s.mode = node->is_dir ? (S_IFDIR | S_IRWXU) : (S_IFREG | S_IRUSR | S_IWUSR);
    s.uid = 0; s.gid = 0;
    memcpy(&file->stat, &s, sizeof(stat_t));

    mutex_unlock(&tmpfs_mutex);
    return 0;
}

static int tmpfs_read(file_t *file, u8 *buf, u32 count) {
    tmpfs_node_t *node = (tmpfs_node_t*)file->private_data;
    if (!node || node->is_dir) return -1;
    u32 remain = node->size - file->pos;
    if (remain <= 0) return 0;
    if (count > remain) count = remain;
    memcpy(buf, node->data + file->pos, count);
    file->pos += count;
    return count;
}

static int tmpfs_write(file_t *file, const u8 *buf, u32 count) {
    tmpfs_node_t *node = (tmpfs_node_t*)file->private_data;
    if (!node || node->is_dir) return -1;

    u32 needed = file->pos + count;
    if (needed > node->capacity) {
        u32 new_cap = node->capacity ? node->capacity * 2 : 64;
        while (new_cap < needed) new_cap *= 2;
        u8 *new_data = (u8*)kmalloc(new_cap);
        if (!new_data) return -1;
        if (node->data) {
            memcpy(new_data, node->data, node->size);
            kfree(node->data);
        }
        node->data = new_data;
        node->capacity = new_cap;
    }

    memcpy(node->data + file->pos, buf, count);
    file->pos += count;
    if (file->pos > node->size) node->size = file->pos;
    stat_t s;
    s.size = node->size; s.mode = S_IFREG | S_IRUSR | S_IWUSR; s.uid = 0; s.gid = 0;
    memcpy(&file->stat, &s, sizeof(stat_t));
    return count;
}

static int tmpfs_readdir(file_t *file, dirent_t *d, int count) {
    tmpfs_node_t *node = (tmpfs_node_t*)file->private_data;
    if (!node || !node->is_dir) return -1;

    int written = 0;
    int pos = file->pos;

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

    int ci = pos - 2;
    while (ci < node->child_count && written < count) {
        int j = 0;
        while (node->children[ci]->name[j] && j < 31) {
            d[written].name[j] = node->children[ci]->name[j]; j++;
        }
        d[written].name[j] = 0;
        d[written].ino = 2 + ci;
        d[written].type = node->children[ci]->is_dir ? 2 : 1;
        written++; file->pos++; ci++;
    }
    return written;
}

static int tmpfs_stat(file_t *file, stat_t *s) {
    memcpy(s, &file->stat, sizeof(stat_t));
    return 0;
}

static int tmpfs_mkdir(const char *path, int mode) {
    mutex_lock(&tmpfs_mutex);

    char parent_path[128];
    char name[32];
    int li = strlen(path) - 1;
    while (li >= 0 && path[li] != '/') li--;
    if (li < 0) { mutex_unlock(&tmpfs_mutex); return -1; }
    memcpy(parent_path, path, li);
    parent_path[li] = 0;
    if (li == 0) { parent_path[0] = '/'; parent_path[1] = 0; }
    memcpy(name, path + li + 1, strlen(path) - li);
    name[strlen(path) - li - 1] = 0;

    tmpfs_node_t *parent = tmpfs_find(parent_path);
    if (!parent || !parent->is_dir) { mutex_unlock(&tmpfs_mutex); return -1; }

    tmpfs_node_t *node = tmpfs_create_node(name, 1);
    if (!node) { mutex_unlock(&tmpfs_mutex); return -1; }
    parent->children[parent->child_count++] = node;

    mutex_unlock(&tmpfs_mutex);
    return 0;
}

static int tmpfs_unlink(const char *path) {
    mutex_lock(&tmpfs_mutex);
    char parent_path[128];
    char name[32];
    int li = strlen(path) - 1;
    while (li >= 0 && path[li] != '/') li--;
    memcpy(parent_path, path, li);
    parent_path[li] = 0;
    if (li == 0) { parent_path[0] = '/'; parent_path[1] = 0; }
    memcpy(name, path + li + 1, strlen(path) - li);
    name[strlen(path) - li - 1] = 0;

    tmpfs_node_t *parent = tmpfs_find(parent_path);
    if (!parent || !parent->is_dir) { mutex_unlock(&tmpfs_mutex); return -1; }

    for (int i = 0; i < parent->child_count; i++) {
        if (strcmp(parent->children[i]->name, name) == 0) {
            if (parent->children[i]->data) kfree(parent->children[i]->data);
            kfree(parent->children[i]);
            for (int j = i; j < parent->child_count - 1; j++)
                parent->children[j] = parent->children[j + 1];
            parent->child_count--;
            mutex_unlock(&tmpfs_mutex);
            return 0;
        }
    }
    mutex_unlock(&tmpfs_mutex);
    return -1;
}

static vfs_ops_t tmpfs_ops = {
    tmpfs_open, tmpfs_unlink, tmpfs_mkdir
};

void tmpfs_init(void) {
    tmpfs_root.child_count = 0;
    vfs_mount("/tmp", &tmpfs_ops, 0);
}
