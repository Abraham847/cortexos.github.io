#include "fs.h"
#include "ata.h"
#include "heap.h"
#include "task.h"
#include "synch.h"

/* ---- BPB (parsed from boot sector) ---- */
static u16 bps;           /* bytes per sector */
static u8  spc;           /* sectors per cluster */
static u16 res;           /* reserved sectors */
static u8  nfat;          /* number of FATs */
static u16 root_max;      /* max root entries (FAT12/16) */
static u32 tot_sectors;   /* total sectors */
static u16 spf16;         /* sectors per FAT (FAT12/16) */
static u32 spf32;         /* sectors per FAT (FAT32) */
static u32 root_cluster;  /* root dir cluster (FAT32) */
static u32 root_lba;
static u32 data_lba;
static u32 max_cl;
static int fat_type;      /* 12, 16, or 32 */

static u8 *fat;
static int fat_loaded;
static int fat_dirty;

static u16 rdw(const u8 *p) { return p[0] | (p[1] << 8); }
static u32 rdl(const u8 *p) { return p[0]|(p[1]<<8)|(p[2]<<16)|(p[3]<<24); }
static void wrw(u8 *p, u16 v) { p[0]=v&0xFF; p[1]=v>>8; }
static void wrl(u8 *p, u32 v) { p[0]=v&0xFF; p[1]=(v>>8)&0xFF; p[2]=(v>>16)&0xFF; p[3]=(v>>24)&0xFF; }

static int rdsec(u32 lba, int cnt, u8 *buf) { return ata_read(lba, cnt, buf); }
static int wrsec(u32 lba, int cnt, const u8 *buf) { return ata_write(lba, cnt, (void*)buf); }

/* ---- FAT entry access ---- */
static u32 fat_get(u32 cl) {
    if (cl < 2 || cl >= max_cl) return 0x0FFFFFF8;
    if (fat_type == 32) {
        return rdl(fat + cl * 4) & 0x0FFFFFFF;
    } else {
        u32 off = cl * 3 / 2;
        if (cl & 1) return (fat[off]>>4) | ((u32)fat[off+1]<<4);
        return fat[off] | ((u32)(fat[off+1]&0x0F)<<8);
    }
}

static u32 fat_eof(void) { return fat_type == 32 ? 0x0FFFFFF8 : 0xFF8; }
static u32 fat_free(void) { return 0; }

static void fat_set(u32 cl, u32 val) {
    if (fat_type == 32) {
        wrl(fat + cl * 4, val & 0x0FFFFFFF);
    } else {
        u32 off = cl * 3 / 2;
        if (cl & 1) { fat[off]=(fat[off]&0x0F)|((val&0x0F)<<4); fat[off+1]=val>>4; }
        else { fat[off]=val&0xFF; fat[off+1]=(fat[off+1]&0xF0)|((val>>8)&0x0F); }
    }
    fat_dirty = 1;
}

static int fat_sync(void) {
    if (!fat_dirty) return 0;
    u32 spf = fat_type == 32 ? spf32 : spf16;
    if (wrsec(res, spf, fat)) return -1;
    if (nfat > 1 && wrsec(res + spf, spf, fat)) return -1;
    fat_dirty = 0;
    return 0;
}

static u32 cl_lba(u32 cl) { return data_lba + (cl - 2) * spc; }

static u32 cluster_size(void) { return (u32)spc * bps; }

/* ---- Cluster chain operations ---- */
static int cl_read(u32 cl, u8 *buf, u32 max) {
    u32 off = 0;
    u32 csz = cluster_size();
    while (cl >= 2 && cl < max_cl - 1 && off < max) {
        u32 len = csz;
        if (off + len > max) len = max - off;
        int sects = (len + bps - 1) / bps;
        if (rdsec(cl_lba(cl), sects, buf + off)) return -1;
        off += len;
        cl = fat_get(cl);
    }
    return off;
}

static u32 cl_alloc(void) {
    u32 end = (tot_sectors - data_lba) / spc + 2;
    for (u32 i = 2; i < end; i++)
        if (fat_get(i) == 0) { fat_set(i, fat_eof()); return i; }
    return 0;
}

static void cl_free(u32 cl);

static int cl_write(u32 *cl, const u8 *buf, u32 size) {
    u32 off = 0;
    u32 cur = *cl, prev = 0, first = *cl;
    u32 csz = cluster_size();
    u8 z[512];
    while (off < size) {
        if (!cur) {
            cur = cl_alloc();
            if (!cur) { cl_free(first); *cl = 0; return -1; }
            if (prev) fat_set(prev, cur);
            else { *cl = cur; first = cur; }
        }
        u32 chunk = csz;
        if (off + chunk > size) chunk = size - off;
        int sects = (chunk + bps - 1) / bps;
        for (int s = 0; s < sects; s++) {
            for (int i = 0; i < 512; i++) z[i] = 0;
            for (int i = 0; off < size && i < 512; i++) z[i] = buf[off++];
            if (wrsec(cl_lba(cur) + s, 1, z)) { cl_free(first); *cl = 0; return -1; }
        }
        prev = cur;
        cur = fat_get(cur);
        if (cur >= fat_eof()) cur = 0;
    }
    if (prev) fat_set(prev, fat_eof());
    return off;
}

static void cl_free(u32 cl) {
    while (cl >= 2 && cl < max_cl - 1) {
        u32 nxt = fat_get(cl);
        fat_set(cl, 0);
        cl = nxt;
    }
}

/* ---- 8.3 name conversion ---- */
static void name_to_83(const char *name, u8 *out) {
    int i, dot;
    for (i = 0; i < 11; i++) out[i] = ' ';
    dot = 0;
    for (i = 0; name[i]; i++) if (name[i] == '.') { dot = i; break; }
    if (!dot) dot = i;
    for (int i = 0; i < dot && i < 8; i++) out[i] = name[i] >= 'a' && name[i] <= 'z' ? name[i] - 32 : name[i];
    if (name[dot] == '.')
        for (int i = 1; i < 4 && dot + i > 0 && name[dot + i]; i++)
            out[7 + i] = name[dot + i] >= 'a' && name[dot + i] <= 'z' ? name[dot + i] - 32 : name[dot + i];
}

static void name_from_83(const u8 *in, char *out) {
    int j = 0;
    for (int i = 0; i < 8 && in[i] != ' '; i++) out[j++] = in[i];
    if (in[8] != ' ') {
        out[j++] = '.';
        for (int i = 8; i < 11 && in[i] != ' '; i++) out[j++] = in[i];
    }
    out[j] = 0;
}

/* ---- Directory entry operations ---- */
#define DIR_ENTRY_SZ 32
#define DIR_CACHE_MAX 256

static fs_entry_t dir_cache[DIR_CACHE_MAX];
static int dir_cache_count;
static int dir_cache_dirty;

static int parse_dir_sector(const u8 *sec, int max_entries) {
    int n = 0;
    for (int i = 0; i < bps && n < max_entries; i += DIR_ENTRY_SZ) {
        if (sec[i] == 0) return n;
        if (sec[i] == 0xE5) continue;
        if (sec[i + 11] & 0x08) continue; /* volume label */
        name_from_83(sec + i, dir_cache[dir_cache_count + n].name);
        dir_cache[dir_cache_count + n].attr = sec[i + 11];
        dir_cache[dir_cache_count + n].cluster = rdw(sec + i + 26);
        dir_cache[dir_cache_count + n].size = rdl(sec + i + 28);
        n++;
    }
    return n;
}

static int dir_cache_fill(void) {
    dir_cache_count = 0;
    if (fat_type == 32) {
        u32 cl = root_cluster;
        u32 csz = cluster_size();
        u8 *sec = (u8*)kmalloc(bps);
        if (!sec) return -1;
        while (cl >= 2 && cl < max_cl - 1) {
            u32 lba = cl_lba(cl);
            for (u32 s = 0; s < spc && dir_cache_count < DIR_CACHE_MAX; s++) {
                if (rdsec(lba + s, 1, sec)) { kfree(sec); return -1; }
                int n = parse_dir_sector(sec, DIR_CACHE_MAX - dir_cache_count);
                dir_cache_count += n;
                if (n == 0 && s == 0) { kfree(sec); return dir_cache_count; }
            }
            cl = fat_get(cl);
        }
        kfree(sec);
    } else {
        u16 root_sects = (root_max * 32 + bps - 1) / bps;
        for (u16 s = 0; s < root_sects && dir_cache_count < DIR_CACHE_MAX; s++) {
            u8 sec[512];
            if (rdsec(root_lba + s, 1, sec)) return -1;
            dir_cache_count += parse_dir_sector(sec, DIR_CACHE_MAX - dir_cache_count);
        }
    }
    dir_cache_dirty = 0;
    return dir_cache_count;
}

static int find_entry(const char *name, u32 *cl, u32 *size, u8 *attr) {
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
            return i;
        }
    }
    return -1;
}

static int write_dir_entry(int idx, const u8 *data, u32 cl_ofs) {
    u8 sec[512];
    if (fat_type == 32) {
        u32 cl = root_cluster;
        u32 entry_no = 0;
        while (cl >= 2 && cl < max_cl - 1) {
            u32 lba = cl_lba(cl);
            for (u32 s = 0; s < spc; s++) {
                int in_this = bps / DIR_ENTRY_SZ;
                if (idx >= entry_no && idx < entry_no + in_this) {
                    if (rdsec(lba + s, 1, sec)) return -1;
                    int off = (idx - entry_no) * DIR_ENTRY_SZ;
                    for (int i = 0; i < DIR_ENTRY_SZ && i < 512; i++) sec[off + i] = data[i];
                    return wrsec(lba + s, 1, sec);
                }
                entry_no += in_this;
            }
            cl = fat_get(cl);
        }
    } else {
        u16 off = idx * DIR_ENTRY_SZ;
        u16 s = off / bps;
        if (rdsec(root_lba + s, 1, sec)) return -1;
        for (int i = 0; i < DIR_ENTRY_SZ && i < 512; i++) sec[(off % bps) + i] = data[i];
        return wrsec(root_lba + s, 1, sec);
    }
    return -1;
}

static int add_dir_entry(const u8 *data) {
    u8 sec[512];
    if (fat_type == 32) {
        u32 cl = root_cluster;
        u32 entry_no = 0;
        while (cl >= 2 && cl < max_cl - 1) {
            u32 lba = cl_lba(cl);
            for (u32 s = 0; s < spc; s++) {
                if (rdsec(lba + s, 1, sec)) return -1;
                for (int j = 0; j < bps; j += DIR_ENTRY_SZ) {
                    if (sec[j] == 0 || sec[j] == 0xE5) {
                        for (int k = 0; k < DIR_ENTRY_SZ; k++) sec[j + k] = data[k];
                        if (wrsec(lba + s, 1, sec)) return -1;
                        return entry_no + j / DIR_ENTRY_SZ;
                    }
                }
                entry_no += bps / DIR_ENTRY_SZ;
            }
            cl = fat_get(cl);
        }
    } else {
        u16 root_sects = (root_max * DIR_ENTRY_SZ + bps - 1) / bps;
        for (u16 s = 0; s < root_sects; s++) {
            if (rdsec(root_lba + s, 1, sec)) return -1;
            for (int j = 0; j < bps; j += DIR_ENTRY_SZ) {
                if (sec[j] == 0 || sec[j] == 0xE5) {
                    for (int k = 0; k < DIR_ENTRY_SZ; k++) sec[j + k] = data[k];
                    if (wrsec(root_lba + s, 1, sec)) return -1;
                    return s * bps / DIR_ENTRY_SZ + j / DIR_ENTRY_SZ;
                }
            }
        }
    }
    return -1;
}

/* ---- Public API ---- */
int fs_init(void) {
    u8 boot[512];
    if (rdsec(0, 1, boot)) return -1;

    bps = rdw(boot + 11);
    spc = boot[13];
    res = rdw(boot + 14);
    nfat = boot[16];
    root_max = rdw(boot + 17);

    u16 tot16 = rdw(boot + 19);
    u32 tot32 = rdl(boot + 32);
    tot_sectors = tot16 ? tot16 : tot32;

    spf16 = rdw(boot + 22);
    u32 spf = spf16;
    u32 root_cl = 0;

    if (bps == 0 || spc == 0 || tot_sectors == 0) return -1;
    if (bps < 32 || bps > 512 || bps % 32 != 0) return -1;
    if (spc == 0 || (spc & (spc - 1)) != 0) return -1;

    if (spf16 == 0) {
        fat_type = 32;
        spf32 = rdl(boot + 36);
        spf = spf32;
        root_cl = rdl(boot + 44);
        root_cluster = root_cl;
        u32 root_sects = 0;
        root_lba = res + nfat * spf;
        data_lba = root_lba + root_sects;
    } else {
        fat_type = spf == 0 ? 32 : (tot_sectors < 4085 ? 12 : 16);
        u16 root_sects = (root_max * 32 + bps - 1) / bps;
        root_lba = res + nfat * spf;
        data_lba = root_lba + root_sects;
    }

    if (data_lba >= tot_sectors) return -1;
    max_cl = (tot_sectors - data_lba) / spc + 2;
    u32 fat_size = spf * bps;
    fat = (u8*)kmalloc(fat_size);
    if (!fat) return -1;
    if (rdsec(res, spf, fat)) { kfree(fat); return -1; }
    fat_loaded = 1;
    fat_dirty = 0;
    dir_cache_fill();
    return 0;
}

int fs_list(const char *path, fs_entry_t *entries, int max) {
    mutex_lock(&mutex_fs);
    (void)path;
    if (dir_cache_dirty) dir_cache_fill();
    int n = dir_cache_count < max ? dir_cache_count : max;
    for (int i = 0; i < n; i++) entries[i] = dir_cache[i];
    mutex_unlock(&mutex_fs);
    return n;
}

int fs_read(const char *path, u8 *buf, u32 max_size) {
    mutex_lock(&mutex_fs);
    u32 cl; u32 sz; u8 attr;
    int r = -1;
    if (find_entry(path, &cl, &sz, &attr) >= 0 && !(attr & FS_ATTR_DIR)) {
        if (sz > max_size) sz = max_size;
        r = cl_read(cl, buf, sz);
    }
    mutex_unlock(&mutex_fs);
    return r;
}

int fs_write(const char *path, const u8 *buf, u32 size) {
    mutex_lock(&mutex_fs);
    u32 new_cl = 0, old_cl = 0; u32 old_sz = 0; u8 attr = 0;
    int idx = find_entry(path, &old_cl, &old_sz, &attr);
    int written = cl_write(&new_cl, buf, size);
    int ret = -1;

    if (written >= 0) {
        u8 entry[32];
        for (int i = 0; i < 32; i++) entry[i] = 0;
        u8 n83[11]; name_to_83(path, n83);
        for (int i = 0; i < 11; i++) entry[i] = n83[i];
        wrw(entry + 26, new_cl & 0xFFFF);
        wrw(entry + 28, size & 0xFFFF);
        wrw(entry + 30, (size >> 16) & 0xFFFF);
        if (idx >= 0) {
            write_dir_entry(idx, entry, 0);
            if (old_cl) cl_free(old_cl);
            ret = written;
        } else {
            int ni = add_dir_entry(entry);
            if (ni >= 0) ret = written;
        }
        if (ret >= 0) { fat_sync(); dir_cache_dirty = 1; }
    }
    mutex_unlock(&mutex_fs);
    return ret;
}

int fs_delete(const char *path) {
    mutex_lock(&mutex_fs);
    u32 cl = 0;
    int idx = find_entry(path, &cl, 0, 0);
    if (idx < 0) { mutex_unlock(&mutex_fs); return -1; }
    u8 sec[512];
    if (fat_type == 32) {
        u32 entry_no = 0;
        u32 cur = root_cluster;
        while (cur >= 2 && cur < max_cl - 1) {
            u32 lba = cl_lba(cur);
            for (u32 s = 0; s < spc; s++) {
                int in_this = bps / DIR_ENTRY_SZ;
                if (idx >= entry_no && idx < entry_no + in_this) {
                    if (rdsec(lba + s, 1, sec)) { mutex_unlock(&mutex_fs); return -1; }
                    sec[(idx - entry_no) * DIR_ENTRY_SZ] = 0xE5;
                    if (wrsec(lba + s, 1, sec)) { mutex_unlock(&mutex_fs); return -1; }
                    goto deleted;
                }
                entry_no += in_this;
            }
            cur = fat_get(cur);
        }
    } else {
        u16 off = idx * DIR_ENTRY_SZ;
        u16 s = off / bps;
        if (rdsec(root_lba + s, 1, sec)) { mutex_unlock(&mutex_fs); return -1; }
        sec[off % bps] = 0xE5;
        if (wrsec(root_lba + s, 1, sec)) { mutex_unlock(&mutex_fs); return -1; }
    }
deleted:
    if (cl) cl_free(cl);
    fat_sync();
    dir_cache_dirty = 1;
    mutex_unlock(&mutex_fs);
    return 0;
}

int fs_rename(const char *oldp, const char *newp) {
    mutex_lock(&mutex_fs);
    int idx = find_entry(oldp, 0, 0, 0);
    if (idx < 0) { mutex_unlock(&mutex_fs); return -1; }
    u8 n83[11]; name_to_83(newp, n83);
    u8 sec[512];
    if (fat_type == 32) {
        u32 entry_no = 0;
        u32 cur = root_cluster;
        while (cur >= 2 && cur < max_cl - 1) {
            u32 lba = cl_lba(cur);
            for (u32 s = 0; s < spc; s++) {
                int in_this = bps / DIR_ENTRY_SZ;
                if (idx >= entry_no && idx < entry_no + in_this) {
                    if (rdsec(lba + s, 1, sec)) { mutex_unlock(&mutex_fs); return -1; }
                    for (int i = 0; i < 11; i++) sec[(idx - entry_no) * DIR_ENTRY_SZ + i] = n83[i];
                    if (wrsec(lba + s, 1, sec)) { mutex_unlock(&mutex_fs); return -1; }
                    goto renamed;
                }
                entry_no += in_this;
            }
            cur = fat_get(cur);
        }
    } else {
        u16 off = idx * DIR_ENTRY_SZ;
        u16 s = off / bps;
        if (rdsec(root_lba + s, 1, sec)) { mutex_unlock(&mutex_fs); return -1; }
        for (int i = 0; i < 11; i++) sec[(off % bps) + i] = n83[i];
        if (wrsec(root_lba + s, 1, sec)) { mutex_unlock(&mutex_fs); return -1; }
    }
renamed:
    dir_cache_dirty = 1;
    mutex_unlock(&mutex_fs);
    return 0;
}
