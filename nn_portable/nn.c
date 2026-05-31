#include "nn.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---- fixed-point division ---- */
fp fpd(fp a, fp b) {
    if (b == 0) return 0;
    int neg = 0;
    unsigned ua, ub;
    if (a < 0) { ua = -(unsigned)a; neg ^= 1; } else { ua = (unsigned)a; }
    if (b < 0) { ub = -(unsigned)b; neg ^= 1; } else { ub = (unsigned)b; }
    unsigned q = 0;
    for (int i = 0; i < 16; i++) {
        q <<= 1;
        if ((ua >> 31)) { q |= 1; ua = (ua << 1) - ub; }
        else { ua <<= 1; if (ua >= ub) { ua -= ub; q |= 1; } }
    }
    return neg ? -(int)q : (int)q;
}

/* ---- sigmoid LUT ---- */
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

/* ---- matrix ops ---- */
static void mat_init(mat *m, int r, int c) {
    m->r = r; m->c = c;
    m->d = (fp*)calloc((size_t)r * c, sizeof(fp));
    if (!m->d) { fprintf(stderr, "mat_init OOM\n"); exit(1); }
}

static void mat_free(mat *m) { free(m->d); m->d = 0; }

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

static void mat_apply_act(mat *m, nn_act_t act) {
    if (act == ACT_SIGMOID) mat_sigmoid(m);
    else if (act == ACT_RELU) mat_relu(m);
    else mat_tanh(m);
}

static fp act_deriv(fp a_val, nn_act_t act) {
    switch (act) {
        case ACT_RELU: return a_val > 0 ? F1 : 0;
        case ACT_TANH: return F1 - fpm(a_val, a_val);
        default:       return fpm(a_val, F1 - a_val);
    }
}

/* ---- NN init/free ---- */
void nn_init(nn *n, int nl, int *sz) {
    if (nl > NN_MAX_L || nl < 2) return;
    n->nl = nl;
    n->init = 1;
    for (int i = 0; i < nl; i++) {
        n->sz[i] = sz[i];
        n->acts[i] = ACT_SIGMOID;
        mat_init(&n->z[i], sz[i], 1);
        mat_init(&n->a[i], sz[i], 1);
        mat_init(&n->e[i], sz[i], 1);
    }
    for (int i = 0; i < nl - 1; i++) {
        mat_init(&n->w[i], sz[i + 1], sz[i]);
        mat_init(&n->b[i], sz[i + 1], 1);
        mat_init(&n->dw[i], sz[i + 1], sz[i]);
        mat_init(&n->db[i], sz[i + 1], 1);
    }
}

void nn_free(nn *n) {
    if (!n->init) return;
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
            int v = ((i * 1103515245 + l * 987654 + 12345) & 0x7FFFFFFF) % 2001 - 1000;
            n->w[l].d[i] = fpm(FF(v), fpd(s, FF(1000)));
        }
        for (int i = 0; i < n->b[l].r; i++) {
            int v = (((i + l + 7) * 1103515245 + 54321) & 0x7FFFFFFF) % 2001 - 1000;
            n->b[l].d[i] = fpm(FF(v), fpd(s, FF(1000)));
        }
    }
}

/* ---- forward / backward ---- */
void nn_fwd(nn *n, fp *in) {
    for (int i = 0; i < n->sz[0]; i++) n->a[0].d[i] = in[i];
    for (int l = 0; l < n->nl - 1; l++) {
        mat_mul(&n->z[l + 1], &n->w[l], &n->a[l]);
        mat_add(&n->z[l + 1], &n->b[l]);
        mat_copy(&n->a[l + 1], &n->z[l + 1]);
        mat_apply_act(&n->a[l + 1], n->acts[l + 1]);
    }
}

fp nn_bwd(nn *n, fp *targ, fp lr) {
    int L = n->nl - 1;
    for (int i = 0; i < n->sz[L]; i++) n->e[L].d[i] = n->a[L].d[i] - targ[i];
    fp loss = 0;
    for (int i = 0; i < n->sz[L]; i++) loss += fpm(n->e[L].d[i], n->e[L].d[i]);

    for (int l = L; l >= 1; l--) {
        for (int i = 0; i < n->sz[l]; i++)
            n->e[l].d[i] = fpm(n->e[l].d[i], act_deriv(n->a[l].d[i], n->acts[l]));
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

int nn_infer(nn *n, fp *in, fp *out) {
    if (!n->init) return -1;
    nn_fwd(n, in);
    int ol = n->sz[n->nl - 1];
    for (int i = 0; i < ol; i++) out[i] = n->a[n->nl - 1].d[i];
    return 0;
}

/* ---- file I/O (binary, little-endian int32) ---- */
static int count_nn_elems(nn *n) {
    int total = 1 + n->nl + n->nl;
    for (int l = 0; l < n->nl - 1; l++) {
        total += n->w[l].r * n->w[l].c;
        total += n->b[l].r;
    }
    return total;
}

int nn_save_file(nn *n, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    int total = count_nn_elems(n);
    int32_t *buf = (int32_t*)malloc(total * sizeof(int32_t));
    if (!buf) { fclose(f); return -1; }
    int bi = 0;

    buf[bi++] = n->nl;
    for (int i = 0; i < n->nl; i++) buf[bi++] = n->sz[i];
    for (int i = 0; i < n->nl; i++) buf[bi++] = (int)n->acts[i];

    for (int l = 0; l < n->nl - 1; l++) {
        int nw = n->w[l].r * n->w[l].c;
        for (int i = 0; i < nw; i++) buf[bi++] = n->w[l].d[i];
    }
    for (int l = 0; l < n->nl - 1; l++) {
        for (int i = 0; i < n->b[l].r; i++) buf[bi++] = n->b[l].d[i];
    }

    fwrite(buf, sizeof(int32_t), bi, f);
    free(buf);
    fclose(f);
    return 0;
}

int nn_load_file(nn *n, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    int32_t hdr[2 + NN_MAX_L];
    if (fread(hdr, sizeof(int32_t), 1, f) != 1) { fclose(f); return -1; }
    int nl = hdr[0];
    if (nl > NN_MAX_L || nl < 2) { fclose(f); return -1; }

    int hdr_need = 1 + nl + nl;
    int32_t *hdr_buf = (int32_t*)malloc(hdr_need * sizeof(int32_t));
    if (!hdr_buf) { fclose(f); return -1; }
    rewind(f);
    if (fread(hdr_buf, sizeof(int32_t), hdr_need, f) != (size_t)hdr_need) { free(hdr_buf); fclose(f); return -1; }

    int bi = 0;
    nl = hdr_buf[bi++];
    int sz[NN_MAX_L];
    for (int i = 0; i < nl; i++) sz[i] = hdr_buf[bi++];

    nn_init(n, nl, sz);
    for (int i = 0; i < nl; i++) n->acts[i] = (nn_act_t)hdr_buf[bi++];
    free(hdr_buf);

    int remain = count_nn_elems(n) - hdr_need;
    if (remain > 0) {
        int32_t *data = (int32_t*)malloc(remain * sizeof(int32_t));
        if (!data) { fclose(f); return -1; }
        if (fread(data, sizeof(int32_t), remain, f) != (size_t)remain) { free(data); fclose(f); return -1; }
        int di = 0;
        for (int l = 0; l < n->nl - 1; l++) {
            int nw = n->w[l].r * n->w[l].c;
            for (int i = 0; i < nw; i++) n->w[l].d[i] = data[di++];
        }
        for (int l = 0; l < n->nl - 1; l++) {
            for (int i = 0; i < n->b[l].r; i++) n->b[l].d[i] = data[di++];
        }
        free(data);
    }
    fclose(f);
    return 0;
}

/* ---- text export ---- */
static int itoa_write(int val, char *buf, int max) {
    char tmp[16]; int i = 0, neg = 0, pos = 0;
    if (max <= 0) return 0;
    unsigned uv;
    if (val < 0) { neg = 1; uv = -(unsigned)val; } else { uv = (unsigned)val; }
    if (uv == 0) { if (max > 1) { buf[0] = '0'; buf[1] = 0; } return 1; }
    while (uv && i < 15) { tmp[i++] = '0' + (uv % 10); uv /= 10; }
    if (neg && pos < max - 1) buf[pos++] = '-';
    while (i && pos < max - 1) buf[pos++] = tmp[--i];
    if (pos < max) buf[pos] = 0;
    return pos;
}

int nn_export_txt(nn *n, char *buf, int max) {
    int pos = 0;
    pos += itoa_write(n->nl, buf + pos, max - pos);
    if (pos >= max - 2) return -1;
    buf[pos++] = '\n';
    for (int i = 0; i < n->nl; i++) {
        if (i > 0) buf[pos++] = ' ';
        pos += itoa_write(n->sz[i], buf + pos, max - pos);
    }
    if (pos >= max - 2) return -1;
    buf[pos++] = '\n';
    for (int l = 0; l < n->nl - 1; l++) {
        int nw = n->w[l].r * n->w[l].c;
        for (int i = 0; i < nw; i++) {
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
        for (int i = 0; i < n->b[l].r; i++) {
            if (pos + 20 >= max) { buf[pos] = 0; return pos; }
            int v = FI(n->b[l].d[i]);
            buf[pos++] = 'b'; buf[pos++] = ' ';
            pos += itoa_write(l, buf + pos, max - pos);
            buf[pos++] = ' ';
            pos += itoa_write(i, buf + pos, max - pos);
            buf[pos++] = ' ';
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

/* ---- dataset ---- */
int ds_load(dataset *ds, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    int32_t header[3];
    if (fread(header, sizeof(int32_t), 3, f) != 3) { fclose(f); return -1; }
    ds->n = header[0]; ds->ni = header[1]; ds->no = header[2];
    if (ds->n <= 0 || ds->ni <= 0 || ds->no <= 0) { fclose(f); return -1; }
    long long total_ll = (long long)ds->n * (ds->ni + ds->no);
    if (total_ll > 0x7FFFFFFFLL / (long long)sizeof(int32_t)) { fclose(f); return -1; }
    int total = (int)total_ll;
    int32_t *raw = (int32_t*)malloc((size_t)total * sizeof(int32_t));
    if (!raw) { fclose(f); return -1; }
    size_t nr = fread(raw, sizeof(int32_t), total, f);
    if (nr != (size_t)total) { free(raw); fclose(f); return -1; }
    fclose(f);
    ds->in = (fp*)malloc(ds->n * ds->ni * sizeof(fp));
    ds->out = (fp*)malloc(ds->n * ds->no * sizeof(fp));
    int ri = 0;
    for (int i = 0; i < ds->n; i++) {
        for (int j = 0; j < ds->ni; j++) ds->in[i * ds->ni + j] = raw[ri++];
        for (int j = 0; j < ds->no; j++) ds->out[i * ds->no + j] = raw[ri++];
    }
    free(raw);
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

int ds_export_text(dataset *ds, const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    for (int i = 0; i < ds->n; i++) {
        for (int j = 0; j < ds->ni; j++) {
            if (j > 0) fputc(' ', f);
            fprintf(f, "%d", FI(ds->in[i * ds->ni + j]));
        }
        fputc(' ', f);
        for (int j = 0; j < ds->no; j++) {
            if (j > 0) fputc(' ', f);
            fprintf(f, "%d", FI(ds->out[i * ds->no + j]));
        }
        fputc('\n', f);
    }
    fclose(f);
    return 0;
}

int ds_import_text(dataset *ds, const char *path, int ni, int no) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int lines = 0, c;
    while ((c = fgetc(f)) != EOF) if (c == '\n') lines++;
    rewind(f);
    if (lines == 0) { fclose(f); return -1; }
    ds->n = lines; ds->ni = ni; ds->no = no;
    ds->in = (fp*)malloc(lines * ni * sizeof(fp));
    ds->out = (fp*)malloc(lines * no * sizeof(fp));
    for (int i = 0; i < lines; i++) {
        int vals[32], nv = 0;
        char line[256];
        if (!fgets(line, 256, f)) break;
        char *p = line;
        while (*p && nv < 32) {
            while (*p == ' ' || *p == '\t') p++;
            if ((*p < '0' || *p > '9') && *p != '-') { p++; continue; }
            int val = 0, neg = 0;
            if (*p == '-') { neg = 1; p++; }
            while (*p >= '0' && *p <= '9') val = val * 10 + (*p++ - '0');
            vals[nv++] = neg ? -val : val;
        }
        for (int j = 0; j < ni && j < nv; j++) ds->in[i * ni + j] = FF(vals[j]);
        for (int j = ni; j < nv && j - ni < no; j++) ds->out[i * no + (j - ni)] = FF(vals[j]);
    }
    fclose(f);
    return 0;
}
