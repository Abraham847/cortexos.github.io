#ifndef MODEL_H
#define MODEL_H

#include "nn.h"

#define MODEL_MAX 8
#define MODEL_NAME_MAX 16

typedef struct {
    char name[MODEL_NAME_MAX];
    nn *net;
    int refs;
    int used;
} model_entry_t;

void model_init(void);
int model_register(const char *name, nn *net);
int model_load_file(const char *name, const char *path);
nn* model_get(const char *name);
int model_put(const char *name);
int model_unregister(const char *name);
void model_list(char *buf, int max);
int model_count(void);
const char *model_name_at(int idx);
int model_refs_at(int idx);

#endif
