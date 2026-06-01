#include "fs.h"
#include "ata.h"
#include "heap.h"

static struct {
    u16 bps; u8 spc; u16 res; u8 nfat; u16 root_max;
    u16 tot; u8 media; u16 spf; u16 root_lba; u16 data_lba;
} bpb;

static u8 *fat;
static int fat_loaded;
static int fat_dirty;

static u16 rdw(const u8 *p) { return p[0] | (p[1] << 8); }
static void wrw(u8 *p, u16 v) { p[0] = v & 0xFF; p[1] = v >> 8; }

static int rdsec(u32 lba, u8 *buf) { return ata_read(lba, 1, buf); }
static int wrsec(u32 lba, const u8 *buf) { return ata_write(lba, 1, (void*)buf); }

static u16 max_cl;

static u16 fat_get(u16 cl) {
    if (cl >= max_cl) return 0xFF8;
    u32 off = cl * 3 / 2;
    if (cl & 1) return (fat[off] >> 4) | ((u16)fat[off+1] << 4);
    return fat[off] | ((u16)(fat[off+1] & 0x0F) << 8);
}

static void fat_set(u16 cl, u16 val) {
    u32 off = cl * 3 / 2;
    if (cl & 1) { fat[off] = (fat[off] & 0x0F) | ((val & 0x0F) << 4); fat[off+1] = val >> 4; }
    else { fat[off] = val & 0xFF; fat[off+1] = (fat[off+1] & 0xF0) | ((val >> 8) & 0x0F); }
    fat_dirty = 1;
}

static int fat_sync(void) {
    if (!fat_dirty) return 0;
    if (ata_write(1, bpb.spf, fat)) return -1;
    if (ata_write(1 + bpb.spf, bpb.spf, fat)) return -1;
    fat_dirty = 0;
    return 0;
}

static u16 cl_lba(u16 cl) { return bpb.data_lba + (cl - 2) * bpb.spc; }

static int cl_read(u16 cl, u8 *buf, u32 max) {
    u32 off = 0;
    while (cl >= 2 && cl < 0xFF8 && off < max) {
        u32 len = bpb.spc * bpb.bps;
        if (off + len > max) len = max - off;
        if (ata_read(cl_lba(cl), (len + bpb.bps - 1) / bpb.bps, buf + off)) return -1;
        off += len;
        cl = fat_get(cl);
    }
    return off;
}

static u16 cl_alloc(void) {
    for (u16 i = 2; i < bpb.tot / bpb.spc; i++)
        if (fat_get(i) == 0) { fat_set(i, 0xFFF); return i; }
    return 0;
}

static int cl_write(u16 *cl, const u8 *buf, u32 size) {
    u32 off = 0;
    u16 cur = *cl, prev = 0;
    u8 *tmp = (u8*)kmalloc(512);
    if (!tmp) return -1;
    while (off < size) {
        if (!cur) { cur = cl_alloc(); if (!cur) { kfree(tmp); return -1; } if (prev) fat_set(prev, cur); else *cl = cur; }
        u32 chunk = bpb.spc * bpb.bps;
        if (off + chunk > size) chunk = size - off;
        int sects = (chunk + bpb.bps - 1) / bpb.bps;
        for (int s = 0; s < sects; s++) {
            for (int i = 0; i < 512; i++) tmp[i] = 0;
            for (int i = 0; i < 512 && off < size; i++) tmp[i] = buf[off++];
            if (ata_write(cl_lba(cur) + s, 1, tmp)) { kfree(tmp); return -1; }
        }
        prev = cur; cur = fat_get(cur);
        if (cur >= 0xFF8) cur = 0;
    }
    if (prev) fat_set(prev, 0xFFF);
    kfree(tmp);
    return off;
}

static void cl_free(u16 cl) {
    while (cl >= 2 && cl < 0xFF8) { u16 nxt = fat_get(cl); fat_set(cl, 0); cl = nxt; }
}

static void name_to_83(const char *name, u8 *out) {
    int i;
    for (i = 0; i < 11; i++) out[i] = ' ';
    int dot = 0; for (i = 0; name[i]; i++) if (name[i] == '.') { dot = i; break; }
    if (!dot) dot = i;
    for (i = 0; i < dot && i < 8; i++) out[i] = name[i] >= 'a' && name[i] <= 'z' ? name[i] - 32 : name[i];
    if (name[dot] == '.')
        for (i = 1; i < 4 && dot + i > 0 && name[dot + i]; i++)
            out[7 + i] = name[dot + i] >= 'a' && name[dot + i] <= 'z' ? name[dot + i] - 32 : name[dot + i];
}

static void name_from_83(const u8 *in, char *out) {
    int i, j = 0;
    for (i = 0; i < 8 && in[i] != ' '; i++) out[j++] = in[i];
    if (in[8] != ' ') { out[j++] = '.'; for (i = 8; i < 11 && in[i] != ' '; i++) out[j++] = in[i]; }
    out[j] = 0;
}

#define DIR_CACHE_MAX 224
static fs_entry_t dir_cache[DIR_CACHE_MAX];
static int dir_cache_off[DIR_CACHE_MAX];
static int dir_cache_count;
static int dir_cache_dirty;

static int dir_cache_fill(void) {
    u8 sector[512];
    u16 root_sects = (bpb.root_max * 32 + bpb.bps - 1) / bpb.bps;
    int count = 0;
    for (u16 s = 0; s < root_sects && count < DIR_CACHE_MAX; s++) {
        if (rdsec(bpb.root_lba + s, sector)) return -1;
        for (int i = 0; i < 512 && count < DIR_CACHE_MAX; i += 32) {
            if (sector[i] == 0) goto done;
            if (sector[i] == 0xE5) continue;
            if (sector[i + 11] & FS_ATTR_LABEL) continue;
            name_from_83(sector + i, dir_cache[count].name);
            dir_cache[count].attr = sector[i + 11];
            dir_cache[count].cluster = rdw(sector + i + 26);
            dir_cache[count].size = rdw(sector + i + 28) | ((u32)rdw(sector + i + 30) << 16);
            dir_cache_off[count] = s * 512 + i;
            count++;
        }
    }
done:
    dir_cache_count = count;
    dir_cache_dirty = 0;
    return count;
}

static int find_entry(const char *name, u16 *cl, u32 *size, u8 *attr) {
    u8 name83[11]; name_to_83(name, name83);
    if (dir_cache_dirty) dir_cache_fill();
    for (int i = 0; i < dir_cache_count; i++) {
        char cname[13];
        name_to_83(dir_cache[i].name, (u8*)cname);
        int match = 1;
        for (int j = 0; j < 11; j++) if (cname[j] != name83[j]) { match = 0; break; }
        if (match) {
            if (cl) *cl = dir_cache[i].cluster;
            if (size) *size = dir_cache[i].size;
            if (attr) *attr = dir_cache[i].attr;
            return dir_cache_off[i];
        }
    }
    return -1;
}

static int write_entry(int offset, const u8 *data) {
    u8 sector[512];
    u16 s = offset / 512;
    if (rdsec(bpb.root_lba + s, sector)) return -1;
    for (int i = 0; i < 32 && i < 512; i++) sector[(offset % 512) + i] = data[i];
    return wrsec(bpb.root_lba + s, sector);
}

int fs_init(void) {
    u8 boot[512];
    if (ata_read(0, 1, boot)) return -1;
    bpb.bps = rdw(boot + 11);
    bpb.spc = boot[13];
    bpb.res = rdw(boot + 14);
    bpb.nfat = boot[16];
    bpb.root_max = rdw(boot + 17);
    bpb.tot = rdw(boot + 19);
    bpb.media = boot[21];
    bpb.spf = rdw(boot + 22);
    if (bpb.bps < 512 || bpb.spc == 0 || bpb.tot == 0) return -1;
    bpb.root_lba = bpb.res + bpb.nfat * bpb.spf;
    u16 root_sects = (bpb.root_max * 32 + bpb.bps - 1) / bpb.bps;
    bpb.data_lba = bpb.root_lba + root_sects;
    max_cl = (bpb.tot - bpb.data_lba) / bpb.spc + 2;
    u32 fat_size = bpb.spf * bpb.bps;
    fat = (u8*)kmalloc(fat_size);
    if (!fat) return -1;
    if (ata_read(1, bpb.spf, fat)) { kfree(fat); return -1; }
    fat_loaded = 1;
    fat_dirty = 0;
    dir_cache_fill();
    return 0;
}

int fs_list(const char *path, fs_entry_t *entries, int max) {
    (void)path;
    if (dir_cache_dirty) dir_cache_fill();
    int n = dir_cache_count < max ? dir_cache_count : max;
    for (int i = 0; i < n; i++) entries[i] = dir_cache[i];
    return n;
}

int fs_read(const char *path, u8 *buf, u32 max_size) {
    u16 cl; u32 sz; u8 attr;
    if (find_entry(path, &cl, &sz, &attr) < 0) return -1;
    if (attr & FS_ATTR_DIR) return -1;
    if (sz > max_size) sz = max_size;
    return cl_read(cl, buf, sz);
}

int fs_write(const char *path, const u8 *buf, u32 size) {
    u16 new_cl = 0, old_cl = 0; u32 old_sz = 0; u8 attr = 0;
    int off = find_entry(path, &old_cl, &old_sz, &attr);
    int written = cl_write(&new_cl, buf, size);
    if (written < 0) return -1;
    u8 entry[32]; int i;
    for (i = 0; i < 32; i++) entry[i] = 0;
    u8 n83[11]; name_to_83(path, n83);
    for (i = 0; i < 11; i++) entry[i] = n83[i];
    wrw(entry + 26, new_cl);
    wrw(entry + 28, size & 0xFFFF);
    wrw(entry + 30, (size >> 16) & 0xFFFF);
    if (off >= 0) {
        write_entry(off, entry);
        if (old_cl) cl_free(old_cl);
    } else {
        u8 sector[512];
        u16 root_sects = (bpb.root_max * 32 + bpb.bps - 1) / bpb.bps;
        int found = 0;
        for (u16 s = 0; s < root_sects && !found; s++) {
            if (rdsec(bpb.root_lba + s, sector)) return -1;
            for (int j = 0; j < 512; j += 32) {
                if (sector[j] == 0 || sector[j] == 0xE5) {
                    for (int k = 0; k < 32; k++) sector[j + k] = entry[k];
                    if (wrsec(bpb.root_lba + s, sector)) return -1;
                    found = 1; break;
                }
            }
        }
        if (!found) return -1;
    }
    fat_sync();
    dir_cache_dirty = 1;
    return written;
}

int fs_delete(const char *path) {
    u16 cl = 0;
    int off = find_entry(path, &cl, 0, 0);
    if (off < 0) return -1;
    u8 sector[512];
    u16 s = off / 512;
    if (rdsec(bpb.root_lba + s, sector)) return -1;
    sector[off % 512] = 0xE5;
    if (wrsec(bpb.root_lba + s, sector)) return -1;
    if (cl) cl_free(cl);
    fat_sync();
    dir_cache_dirty = 1;
    return 0;
}

int fs_rename(const char *oldp, const char *newp) {
    int off = find_entry(oldp, 0, 0, 0);
    if (off < 0) return -1;
    u8 n83[11]; name_to_83(newp, n83);
    u8 sector[512];
    u16 s = off / 512;
    if (rdsec(bpb.root_lba + s, sector)) return -1;
    for (int i = 0; i < 11; i++) sector[(off % 512) + i] = n83[i];
    if (wrsec(bpb.root_lba + s, sector)) return -1;
    dir_cache_dirty = 1;
    return 0;
}
