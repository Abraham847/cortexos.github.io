#include "kernel.h"
#include "kernel_api.h"
#include "loader.h"
#include "fs.h"

int app_load(const char *path, const kernel_api_t *api) {
    int nr = fs_read(path, (u8*)APP_BASE, 32768);
    if (nr < 64) return -1;
    u8 *magic = (u8*)APP_BASE;
    if (magic[0] != 0x55 || magic[1] != 0x89) return -1;
    void (*entry)(const kernel_api_t*) = (void (*)(const kernel_api_t*))APP_BASE;
    entry(api);
    return 0;
}
