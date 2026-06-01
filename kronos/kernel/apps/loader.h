#ifndef LOADER_H
#define LOADER_H

#include "kernel_api.h"

#define APP_BASE 0x50000

int app_load(const char *path, const kernel_api_t *api);

#endif
