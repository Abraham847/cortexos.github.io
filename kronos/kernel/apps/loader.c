#include "kernel.h"
#include "kernel_api.h"
#include "loader.h"
#include "fs.h"

static u8 app_csum(const u8 *data, int len) {
    u8 c = 0;
    for (int i = 0; i < len; i++) c ^= data[i];
    return c;
}

int app_load(const char *path, const kernel_api_t *api) {
    int nr = fs_read(path, (u8*)APP_BASE, 32768);
    if (nr < 64) return -1;
    u8 *data = (u8*)APP_BASE;
    if (data[0] != 0x55 || data[1] != 0x89) return -1;
    int size = nr - 1;
    u8 stored_csum = data[size];
    u8 calc_csum = app_csum(data, size);
    if (stored_csum != calc_csum) return -1;
    void (*entry)(const kernel_api_t*) = (void (*)(const kernel_api_t*))APP_BASE;
    entry(api);
    return 0;
}
