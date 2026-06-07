#include "model.h"
#include "heap.h"
#include "fs.h"
#include "task.h"
#include "synch.h"

static model_entry_t registry[MODEL_MAX];

void model_init(void) {
    for (int i = 0; i < MODEL_MAX; i++) registry[i].used = 0;
}

static int find_slot(void) {
    for (int i = 0; i < MODEL_MAX; i++)
        if (!registry[i].used) return i;
    return -1;
}

static int find_name(const char *name) {
    for (int i = 0; i < MODEL_MAX; i++)
        if (registry[i].used && strcmp(registry[i].name, name) == 0) return i;
    return -1;
}

int model_register(const char *name, nn *net) {
    mutex_lock(&mutex_model);
    int si = find_slot();
    if (si < 0) { mutex_unlock(&mutex_model); return -1; }
    int j;
    for (j = 0; name[j] && j < MODEL_NAME_MAX - 1; j++)
        registry[si].name[j] = name[j];
    registry[si].name[j] = 0;
    registry[si].net = net;
    registry[si].refs = 1;
    registry[si].used = 1;
    mutex_unlock(&mutex_model);
    return 0;
}

int model_load_file(const char *name, const char *path) {
    nn *n = (nn*)kmalloc(sizeof(nn));
    if (!n) return -1;
    if (nn_load_file(n, path) != 0) { kfree(n); return -1; }
    int r = model_register(name, n);
    if (r != 0) { nn_free(n); kfree(n); return -1; }
    return 0;
}

nn* model_get(const char *name) {
    mutex_lock(&mutex_model);
    int idx = find_name(name);
    if (idx < 0) { mutex_unlock(&mutex_model); return 0; }
    if (registry[idx].refs < 0x7FFFFFFF) registry[idx].refs++;
    nn *net = registry[idx].net;
    mutex_unlock(&mutex_model);
    return net;
}

int model_put(const char *name) {
    mutex_lock(&mutex_model);
    int idx = find_name(name);
    if (idx < 0) { mutex_unlock(&mutex_model); return -1; }
    if (registry[idx].refs > 0) registry[idx].refs--;
    int r = registry[idx].refs;
    mutex_unlock(&mutex_model);
    return r;
}

int model_unregister(const char *name) {
    mutex_lock(&mutex_model);
    int idx = find_name(name);
    if (idx < 0) { mutex_unlock(&mutex_model); return -1; }
    if (registry[idx].refs > 0) { mutex_unlock(&mutex_model); return -1; }
    nn_free(registry[idx].net);
    kfree(registry[idx].net);
    registry[idx].used = 0;
    mutex_unlock(&mutex_model);
    return 0;
}

void model_list(char *buf, int max) {
    mutex_lock(&mutex_model);
    int pos = 0;
    for (int i = 0; i < MODEL_MAX; i++) {
        if (!registry[i].used) continue;
        char tmp[16];
        int j;
        for (j = 0; registry[i].name[j] && pos < max - 2; j++)
            buf[pos++] = registry[i].name[j];
        if (pos < max - 1) buf[pos++] = ' ';
        itoa(registry[i].refs, tmp);
        for (j = 0; tmp[j] && pos < max - 2; j++) buf[pos++] = tmp[j];
        if (pos < max - 1) buf[pos++] = ' ';
    }
    if (max > 0) buf[max - 1] = 0;
    if (pos < max) buf[pos] = 0;
    mutex_unlock(&mutex_model);
}

int model_count(void) {
    mutex_lock(&mutex_model);
    int n = 0;
    for (int i = 0; i < MODEL_MAX; i++) if (registry[i].used) n++;
    mutex_unlock(&mutex_model);
    return n;
}

const char *model_name_at(int idx) {
    mutex_lock(&mutex_model);
    if (idx < 0 || idx >= MODEL_MAX || !registry[idx].used) { mutex_unlock(&mutex_model); return 0; }
    const char *n = registry[idx].name;
    mutex_unlock(&mutex_model);
    return n;
}

int model_refs_at(int idx) {
    mutex_lock(&mutex_model);
    if (idx < 0 || idx >= MODEL_MAX || !registry[idx].used) { mutex_unlock(&mutex_model); return -1; }
    int r = registry[idx].refs;
    mutex_unlock(&mutex_model);
    return r;
}
