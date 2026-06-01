#include "nn.h"
#include "heap.h"
#include "ata.h"
#include "fs.h"

fp fpd(fp a, fp b) {
    if (b == 0) return 0;
    int neg = 0;
    unsigned long long ua, ub;
    if (a < 0) { ua = -(unsigned long long)a; neg ^= 1; } else { ua = (unsigned long long)a; }
    if (b < 0) { ub = -(unsigned long long)b; neg ^= 1; } else { ub = (unsigned long long)b; }
    unsigned q = 0;
    for (int i = 0; i < 16; i++) {
        q <<= 1;
        if ((ua >> 31)) { q |= 1; ua = (ua << 1) - ub; }
        else { ua <<= 1; if (ua >= ub) { ua -= ub; q |= 1; } }
    }
    return neg ? -(int)q : (int)q;
}

static int itoa_write(int val, char *buf, int max);

static fp sigmoid(fp x) {
    static const int lut[33] = {
        65536, 51034, 39750, 30957, 24108, 18776, 14624, 11388,
        8871, 6909, 5381, 4191, 3264, 2542, 1980, 1542,
        1201, 935, 728, 567, 442, 344, 268, 209,
        163, 127, 99, 77, 60, 47, 36, 28, 22
    };
    if (x <= FF(-8)) return 0;
    if (x >= FF(8)) return F1;
    int inv = 0;
    if (x < 0) { inv = 1; x = -x; }
    int idx = x / FF(0.25);
    if (idx > 31) idx = 31;
    fp rem = x - fpm(FF(idx), FF(0.25));
    fp ex = lut[idx] + fpm(lut[idx + 1] - lut[idx], fpd(rem, FF(0.25)));
    fp r = fpd(F1, F1 + ex);
    return inv ? F1 - r : r;
}

static fp tanh_fp(fp x) {
    fp s = sigmoid(fpm(x, FF(2)));
    return fpm(FF(2), s) - F1;
}

static int mat_init(mat *m, int r, int c) {
    m->r = r; m->c = c;
    m->d = (fp *)kmalloc(r * c * sizeof(fp));
    if (!m->d) { m->r = 0; m->c = 0; return -1; }
    return 0;
}

static void mat_free(mat *m) {
    kfree(m->d); m->d = 0;
}

static void mat_mul(mat *c, mat *a, mat *b) {
    for (int i = 0; i < a->r; i++)
        for (int j = 0; j < b->c; j++) {
            fp sum = 0;
            for (int k = 0; k < a->c; k++)
                sum += fpm(a->d[i * a->c + k], b->d[k * b->c + j]);
            c->d[i * c->c + j] = sum;
        }
}

static void mat_add(mat *a, mat *b) {
    for (int i = 0; i < a->r * a->c; i++) a->d[i] += b->d[i];
}

static void mat_copy(mat *dst, mat *src) {
    for (int i = 0; i < src->r * src->c; i++) dst->d[i] = src->d[i];
}

static void mat_sub(mat *a, mat *b) {
    for (int i = 0; i < a->r * a->c; i++) a->d[i] -= b->d[i];
}

static void mat_sigmoid(mat *m) {
    for (int i = 0; i < m->r * m->c; i++) m->d[i] = sigmoid(m->d[i]);
}

static void mat_relu(mat *m) {
    for (int i = 0; i < m->r * m->c; i++)
        if (m->d[i] < 0) m->d[i] = 0;
}

static void mat_tanh(mat *m) {
    for (int i = 0; i < m->r * m->c; i++)
        m->d[i] = tanh_fp(m->d[i]);
}

static void mat_scale(mat *m, fp s) {
    for (int i = 0; i < m->r * m->c; i++) m->d[i] = fpm(m->d[i], s);
}

static void mat_mul_t(mat *c, mat *a, mat *b) {
    for (int i = 0; i < a->r; i++)
        for (int j = 0; j < b->r; j++) {
            fp sum = 0;
            for (int k = 0; k < a->c; k++)
                sum += fpm(a->d[i * a->c + k], b->d[j * b->c + k]);
            c->d[i * c->c + j] = sum;
        }
}

static fp apply_act(fp x, nn_act_t act) {
    switch (act) {
        case ACT_RELU: return x < 0 ? 0 : x;
        case ACT_TANH: return tanh_fp(x);
        default:       return sigmoid(x);
    }
}

static void mat_apply_act(mat *m, nn_act_t act) {
    if (act == ACT_SIGMOID) { mat_sigmoid(m); return; }
    if (act == ACT_RELU)    { mat_relu(m);    return; }
    if (act == ACT_TANH)    { mat_tanh(m);    return; }
}

static fp act_deriv(fp a_val, nn_act_t act) {
    switch (act) {
        case ACT_RELU: return a_val > 0 ? F1 : 0;
        case ACT_TANH: return F1 - fpm(a_val, a_val);
        default:       return fpm(a_val, F1 - a_val);
    }
}

int nn_init(nn *n, int nl, int *sz) {
    if (nl <= 0 || nl > NN_MAX_L) return -1;
    n->nl = nl;
    n->init = 0;
    for (int i = 0; i < nl; i++) {
        n->sz[i] = sz[i];
        n->acts[i] = ACT_SIGMOID;
        if (mat_init(&n->z[i], sz[i], 1)) { nn_free(n); return -1; }
        if (mat_init(&n->a[i], sz[i], 1)) { nn_free(n); return -1; }
        if (mat_init(&n->e[i], sz[i], 1)) { nn_free(n); return -1; }
    }
    for (int i = 0; i < nl - 1; i++) {
        if (mat_init(&n->w[i], sz[i + 1], sz[i])) { nn_free(n); return -1; }
        if (mat_init(&n->b[i], sz[i + 1], 1)) { nn_free(n); return -1; }
        if (mat_init(&n->dw[i], sz[i + 1], sz[i])) { nn_free(n); return -1; }
        if (mat_init(&n->db[i], sz[i + 1], 1)) { nn_free(n); return -1; }
    }
    n->init = 1;
    return 0;
}

void nn_free(nn *n) {
    if (!n || !n->init) return;
    for (int i = 0; i < n->nl; i++) {
        mat_free(&n->z[i]);
        mat_free(&n->a[i]);
        mat_free(&n->e[i]);
    }
    for (int i = 0; i < n->nl - 1; i++) {
        mat_free(&n->w[i]);
        mat_free(&n->b[i]);
        mat_free(&n->dw[i]);
        mat_free(&n->db[i]);
    }
    n->init = 0;
}

void nn_rand(nn *n, fp s) {
    for (int l = 0; l < n->nl - 1; l++) {
        int nw = n->w[l].r * n->w[l].c;
        for (int i = 0; i < nw; i++) {
            unsigned v = ((unsigned)i * 1103515245u + (unsigned)l * 987654u + 12345u) & 0x7FFFFFFFu;
            n->w[l].d[i] = fpm(FF((int)v % 2001 - 1000), fpd(s, FF(1000)));
        }
        for (int i = 0; i < n->b[l].r; i++) {
            unsigned vu = ((unsigned)(i + l + 7) * 1103515245u + 54321u) & 0x7FFFFFFFu;
            n->b[l].d[i] = fpm(FF((int)(vu % 2001) - 1000), fpd(s, FF(1000)));
        }
    }
}

void nn_fwd(nn *n, fp *in) {
    for (int i = 0; i < n->sz[0]; i++) n->a[0].d[i] = in[i];
    for (int l = 0; l < n->nl - 1; l++) {
        mat_mul(&n->z[l + 1], &n->w[l], &n->a[l]);
        mat_add(&n->z[l + 1], &n->b[l]);
        mat_copy(&n->a[l + 1], &n->z[l + 1]);
        if (n->acts[l + 1] == ACT_SIGMOID) mat_sigmoid(&n->a[l + 1]);
        else if (n->acts[l + 1] == ACT_RELU) mat_relu(&n->a[l + 1]);
        else mat_tanh(&n->a[l + 1]);
    }
}

fp nn_bwd(nn *n, fp *targ, fp lr) {
    int L = n->nl - 1;
    for (int i = 0; i < n->sz[L]; i++) n->e[L].d[i] = n->a[L].d[i] - targ[i];
    fp loss = 0;
    for (int i = 0; i < n->sz[L]; i++) loss += fpm(n->e[L].d[i], n->e[L].d[i]);
    for (int l = L; l >= 1; l--) {
        for (int i = 0; i < n->sz[l]; i++) {
            fp a_val = n->a[l].d[i];
            n->e[l].d[i] = fpm(n->e[l].d[i], act_deriv(a_val, n->acts[l]));
        }
        mat_mul_t(&n->dw[l - 1], &n->e[l], &n->a[l - 1]);
        for (int i = 0; i < n->sz[l]; i++) n->db[l - 1].d[i] = n->e[l].d[i];
        if (l > 1) {
            for (int i = 0; i < n->sz[l - 1]; i++) {
                fp sum = 0;
                for (int j = 0; j < n->sz[l]; j++)
                    sum += fpm(n->w[l - 1].d[j * n->w[l - 1].c + i], n->e[l].d[j]);
                n->e[l - 1].d[i] = sum;
            }
        }
    }
    for (int l = 0; l < L; l++) {
        mat_scale(&n->dw[l], lr);
        mat_sub(&n->w[l], &n->dw[l]);
        mat_scale(&n->db[l], lr);
        mat_sub(&n->b[l], &n->db[l]);
    }
    return loss;
}

int nn_save(nn *n, unsigned sector) {
    unsigned char buf[512];
    unsigned char *p = buf;

    *((int *)p) = n->nl; p += 4;
    for (int i = 0; i < n->nl; i++) {
        *((int *)p) = n->sz[i]; p += 4;
    }
    for (int i = 0; i < n->nl; i++) {
        *((int *)p) = (int)n->acts[i]; p += 4;
    }

    for (int l = 0; l < n->nl - 1; l++) {
        int nw = n->w[l].r * n->w[l].c;
        for (int i = 0; i < nw; i++) {
            if ((p - buf) + 4 > 512) {
                if (ata_write(sector++, 1, buf)) return -1;
                p = buf;
            }
            *((int *)p) = n->w[l].d[i]; p += 4;
        }
    }
    for (int l = 0; l < n->nl - 1; l++) {
        for (int i = 0; i < n->b[l].r; i++) {
            if ((p - buf) + 4 > 512) {
                if (ata_write(sector++, 1, buf)) return -1;
                p = buf;
            }
            *((int *)p) = n->b[l].d[i]; p += 4;
        }
    }
    if (p > buf) {
        int pad = p - buf;
        while (pad < 512) { buf[pad] = 0; pad++; }
        if (ata_write(sector, 1, buf)) return -1;
    }
    return 0;
}

int nn_load(nn *n, unsigned sector) {
    unsigned char buf[512];
    if (ata_read(sector++, 1, buf)) return -1;
    unsigned char *p = buf;

    int nl = *((int *)p); p += 4;
    int sz[NN_MAX_L];
    if (nl <= 0 || nl > NN_MAX_L) return -1;
    for (int i = 0; i < nl; i++) {
        sz[i] = *((int *)p); p += 4;
    }

    if (nn_init(n, nl, sz)) return -1;

    for (int i = 0; i < nl; i++) {
        int av = *((int *)p); p += 4;
        if (av < 0 || av > 2) av = 0;
        n->acts[i] = (nn_act_t)av;
    }

    for (int l = 0; l < n->nl - 1; l++) {
        int nw = n->w[l].r * n->w[l].c;
        for (int i = 0; i < nw; i++) {
            if ((p - buf) + 4 > 512) {
                if (ata_read(sector++, 1, buf)) { nn_free(n); return -1; }
                p = buf;
            }
            n->w[l].d[i] = *((int *)p); p += 4;
        }
    }
    for (int l = 0; l < n->nl - 1; l++) {
        for (int i = 0; i < n->b[l].r; i++) {
            if ((p - buf) + 4 > 512) {
                if (ata_read(sector++, 1, buf)) { nn_free(n); return -1; }
                p = buf;
            }
            n->b[l].d[i] = *((int *)p); p += 4;
        }
    }
    return 0;
}

int nn_save_file(nn *n, const char *path) {
    int hdr = 4 + n->nl * 4 + n->nl * 4;
    int wtotal = 0, btotal = 0;
    for (int l = 0; l < n->nl - 1; l++) { wtotal += n->w[l].r * n->w[l].c; }
    for (int l = 0; l < n->nl - 1; l++) { btotal += n->b[l].r; }
    if (wtotal > 262144 / 4 || btotal > 262144 / 4) return -1;
    int total = hdr + wtotal * 4 + btotal * 4;
    unsigned char *buf = (unsigned char*)kmalloc(total);
    if (!buf) return -1;
    unsigned char *p = buf;

    *((int *)p) = n->nl; p += 4;
    for (int i = 0; i < n->nl; i++) { *((int *)p) = n->sz[i]; p += 4; }
    for (int i = 0; i < n->nl; i++) { *((int *)p) = (int)n->acts[i]; p += 4; }

    for (int l = 0; l < n->nl - 1; l++) {
        int nw = n->w[l].r * n->w[l].c;
        for (int i = 0; i < nw; i++) { *((int *)p) = n->w[l].d[i]; p += 4; }
    }
    for (int l = 0; l < n->nl - 1; l++) {
        for (int i = 0; i < n->b[l].r; i++) { *((int *)p) = n->b[l].d[i]; p += 4; }
    }
    int r = fs_write(path, buf, total);
    kfree(buf);
    return r == total ? 0 : -1;
}

int nn_load_file(nn *n, const char *path) {
    unsigned char tmp[256];
    int nread = fs_read(path, tmp, 8);
    if (nread < 8) return -1;
    unsigned char *p = tmp;
    int nl = *((int *)p); p += 4;
    if (nl > NN_MAX_L) return -1;
    int hdr = 4 + nl * 4 + nl * 4;
    int nread2 = fs_read(path, tmp, hdr);
    if (nread2 < hdr) return -1;
    p = tmp + 4;
    int sz[NN_MAX_L];
    for (int i = 0; i < nl; i++) { sz[i] = *((int *)p); p += 4; }
    int wtotal = 0, btotal = 0;
    for (int l = 0; l < nl - 1; l++) { wtotal += sz[l + 1] * sz[l]; }
    for (int l = 0; l < nl - 1; l++) { btotal += sz[l + 1]; }
    if (wtotal > 262144 / 4 || btotal > 262144 / 4) return -1;
    int needed = hdr + wtotal * 4 + btotal * 4;
    unsigned char *buf = (unsigned char*)kmalloc(needed);
    if (!buf) return -1;
    int nread3 = fs_read(path, buf, needed);
    if (nread3 < needed) { kfree(buf); return -1; }
    p = buf + 4;
    for (int i = 0; i < nl; i++) { sz[i] = *((int *)p); p += 4; }
    if (nn_init(n, nl, sz)) { kfree(buf); return -1; }
    for (int i = 0; i < nl; i++) { n->acts[i] = (nn_act_t)(*((int *)p)); p += 4; }
    for (int l = 0; l < n->nl - 1; l++) {
        int nw = n->w[l].r * n->w[l].c;
        for (int i = 0; i < nw; i++) { n->w[l].d[i] = *((int *)p); p += 4; }
    }
    for (int l = 0; l < n->nl - 1; l++) {
        for (int i = 0; i < n->b[l].r; i++) { n->b[l].d[i] = *((int *)p); p += 4; }
    }
    kfree(buf);
    return 0;
}

int nn_export_txt(nn *n, char *buf, int max) {
    int pos = 0;
    int l, i, j;

    pos += itoa_write(n->nl, buf + pos, max - pos);
    if (pos >= max - 2) return -1;
    buf[pos++] = '\n';

    for (i = 0; i < n->nl; i++) {
        if (i > 0) { buf[pos++] = ' '; }
        pos += itoa_write(n->sz[i], buf + pos, max - pos);
    }
    if (pos >= max - 2) return -1;
    buf[pos++] = '\n';

    for (int l = 0; l < n->nl - 1; l++) {
        int nw = n->w[l].r * n->w[l].c;
        for (i = 0; i < nw; i++) {
            if (pos + 20 >= max) { buf[pos] = 0; return pos; }
            int v = FI(n->w[l].d[i]);
            buf[pos++] = 'w'; buf[pos++] = ' ';
            buf[pos++] = '0' + l; buf[pos++] = ' ';
            pos += itoa_write(i, buf + pos, max - pos);
            buf[pos++] = ' ';
            pos += itoa_write(v, buf + pos, max - pos);
            buf[pos++] = '\n';
        }
    }

    for (int l = 0; l < n->nl - 1; l++) {
        for (i = 0; i < n->b[l].r; i++) {
            if (pos + 20 >= max) { buf[pos] = 0; return pos; }
            int v = FI(n->b[l].d[i]);
            buf[pos++] = 'b'; buf[pos++] = ' ';
            buf[pos++] = '0' + l; buf[pos++] = ' ';
            buf[pos++] = '0'; buf[pos++] = ' ';
            pos += itoa_write(v, buf + pos, max - pos);
            buf[pos++] = '\n';
        }
    }

    if (pos >= max) pos = max - 1;
    buf[pos] = 0;
    return pos;
}

const char *nn_act_name(nn_act_t act) {
    switch (act) {
        case ACT_RELU: return "relu";
        case ACT_TANH: return "tanh";
        default:       return "sigmoid";
    }
}

static int itoa_write(int val, char *buf, int max) {
    char tmp[16]; int i = 0, neg = 0, pos = 0;
    if (max <= 0) return 0;
    unsigned u;
    if (val < 0) { neg = 1; u = -(unsigned)val; } else { u = (unsigned)val; }
    if (u == 0) { if (max > 1) { buf[0] = '0'; buf[1] = 0; } return 1; }
    while (u && i < 15) { tmp[i++] = '0' + (u % 10); u /= 10; }
    if (neg && pos < max - 1) buf[pos++] = '-';
    while (i && pos < max - 1) buf[pos++] = tmp[--i];
    if (pos < max) buf[pos] = 0;
    return pos;
}

int ds_load(dataset *ds, const char *path) {
    u8 buf[512];
    int n = fs_read(path, buf, 512);
    if (n < 12) return -1;
    u8 *p = buf;
    ds->n = *(int*)p; p += 4;
    ds->ni = *(int*)p; p += 4;
    ds->no = *(int*)p; p += 4;
    if (ds->n <= 0 || ds->ni <= 0 || ds->no <= 0) return -1;
    if (ds->n > 100000 || ds->ni > 1000 || ds->no > 1000) return -1;
    long long total = (long long)ds->n * (ds->ni + ds->no);
    long long needed = total * 4 + 12;
    if (needed > 0x100000) return -1;
    u8 *big = (u8*)kmalloc((int)needed);
    if (!big) return -1;
    int nread = fs_read(path, big, needed);
    if (nread < needed) { kfree(big); return -1; }
    p = big + 12;
    ds->in = (fp*)kmalloc(ds->n * ds->ni * sizeof(fp));
    ds->out = (fp*)kmalloc(ds->n * ds->no * sizeof(fp));
    if (!ds->in || !ds->out) { kfree(ds->in); kfree(ds->out); kfree(big); return -1; }
    for (int i = 0; i < ds->n; i++) {
        for (int j = 0; j < ds->ni; j++) { ds->in[i * ds->ni + j] = *(int*)p; p += 4; }
        for (int j = 0; j < ds->no; j++) { ds->out[i * ds->no + j] = *(int*)p; p += 4; }
    }
    kfree(big);
    return 0;
}

int ds_train_epoch(nn *n, dataset *ds, fp lr) {
    fp loss = 0;
    for (int i = 0; i < ds->n; i++) {
        nn_fwd(n, ds->in + i * ds->ni);
        loss += nn_bwd(n, ds->out + i * ds->no, lr);
    }
    return loss;
}

int nn_infer(nn *n, fp *in, fp *out) {
    if (!n->init) return -1;
    nn_fwd(n, in);
    int ol = n->sz[n->nl - 1];
    for (int i = 0; i < ol; i++) out[i] = n->a[n->nl - 1].d[i];
    return 0;
}

int ds_export_text(dataset *ds, const char *path) {
    char *buf = (char*)kmalloc(32768);
    if (!buf) return -1;
    int pos = 0;
    for (int i = 0; i < ds->n && pos < 32760; i++) {
        char tmp[16];
        for (int j = 0; j < ds->ni; j++) {
            if (j > 0) buf[pos++] = ' ';
            int v = FI(ds->in[i * ds->ni + j]);
            itoa(v, tmp);
            int k = 0; while (tmp[k] && pos < 32760) buf[pos++] = tmp[k++];
        }
        buf[pos++] = ' ';
        for (int j = 0; j < ds->no; j++) {
            if (j > 0) buf[pos++] = ' ';
            int v = FI(ds->out[i * ds->no + j]);
            itoa(v, tmp);
            int k = 0; while (tmp[k] && pos < 32760) buf[pos++] = tmp[k++];
        }
        buf[pos++] = '\n';
    }
    buf[pos] = 0;
    int r = fs_write(path, (u8*)buf, pos);
    kfree(buf);
    return r == pos ? 0 : -1;
}

int ds_import_text(dataset *ds, const char *path, int ni, int no) {
    char *buf = (char*)kmalloc(32769);
    if (!buf) return -1;
    int nread = fs_read(path, (u8*)buf, 32768);
    if (nread < 4) { kfree(buf); return -1; }
    buf[nread] = 0;
    int lines = 0;
    for (int i = 0; i < nread; i++) if (buf[i] == '\n') lines++;
    if (lines == 0) { kfree(buf); return -1; }
    if (nread > 0 && buf[nread - 1] != '\n') lines++;

    ds->n = lines; ds->ni = ni; ds->no = no;
    ds->in = (fp*)kmalloc(lines * ni * sizeof(fp));
    ds->out = (fp*)kmalloc(lines * no * sizeof(fp));
    if (!ds->in || !ds->out) { kfree(ds->in); kfree(ds->out); kfree(buf); return -1; }
    for (int i = 0; i < lines * ni; i++) ds->in[i] = 0;
    for (int i = 0; i < lines * no; i++) ds->out[i] = 0;

    char *p = buf;
    for (int i = 0; i < lines; i++) {
        int vals[32], nv = 0;
        while (*p && *p != '\n' && nv < 32) {
            while (*p == ' ' || *p == '\t') p++;
            if ((*p < '0' || *p > '9') && *p != '-') { p++; continue; }
            int val = 0, neg = 0;
            if (*p == '-') { neg = 1; p++; }
            while (*p >= '0' && *p <= '9') val = val * 10 + (*p++ - '0');
            vals[nv++] = neg ? -val : val;
        }
        if (*p == '\n') p++;
        for (int j = 0; j < ni && j < nv; j++) ds->in[i * ni + j] = FF(vals[j]);
        for (int j = ni; j < nv && j - ni < no; j++) ds->out[i * no + (j - ni)] = FF(vals[j]);
    }
    kfree(buf);
    return 0;
}
