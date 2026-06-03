#include "nn_float.h"
#include "heap.h"
#include "fs.h"
#include "kpu.h"

static const float sig_lut[33] = {
    1.0f, 0.7788008f, 0.6065307f, 0.4723666f, 0.3678794f, 0.2865048f,
    0.2231302f, 0.1737739f, 0.1353353f, 0.1053992f, 0.0820850f, 0.0639279f,
    0.0497871f, 0.0387742f, 0.0301974f, 0.0235177f, 0.0183156f, 0.0142642f,
    0.0111090f, 0.0086517f, 0.0067379f, 0.0052475f, 0.0040868f, 0.0031828f,
    0.0024788f, 0.0019305f, 0.0015034f, 0.0011709f, 0.0009119f, 0.0007102f,
    0.0005531f, 0.0004309f, 0.0003355f
};

static float sigmoid_f(float x) {
    if (x <= -8.0f) return 0.0f;
    if (x >= 8.0f) return 1.0f;
    int neg = 0;
    if (x < 0) { neg = 1; x = -x; }
    int idx = (int)(x * 4.0f);
    if (idx > 31) idx = 31;
    float rem = x - idx * 0.25f;
    float e = sig_lut[idx] + (sig_lut[idx + 1] - sig_lut[idx]) * rem * 4.0f;
    float r = 1.0f / (1.0f + e);
    return neg ? 1.0f - r : r;
}

static void sigmoid_a(float *a, int n) {
    if (kpu_available()) { kpu_sigmoid_float(a, n); return; }
    for (int i = 0; i < n; i++) a[i] = sigmoid_f(a[i]);
}

static void tanh_a(float *a, int n) {
    for (int i = 0; i < n; i++) a[i] *= 2.0f;
    sigmoid_a(a, n);
    for (int i = 0; i < n; i++) a[i] = a[i] * 2.0f - 1.0f;
}

static void relu_a(float *a, int n) {
    if (kpu_available()) { kpu_relu_float(a, n); return; }
    for (int i = 0; i < n; i++)
        if (a[i] < 0) a[i] = 0;
}

int nnf_init(nn_float_t *n, int nl, int *sz) {
    if (nl <= 0 || nl > NNF_MAX_L) return -1;
    n->nl = nl;
    n->init = 0;
    for (int i = 0; i < NNF_MAX_L; i++) {
        n->z[i] = 0; n->a[i] = 0; n->e[i] = 0;
        if (i < NNF_MAX_L - 1) { n->w[i] = 0; n->b[i] = 0; n->dw[i] = 0; n->db[i] = 0; }
    }
    for (int i = 0; i < nl; i++) {
        n->sz[i] = sz[i];
        n->z[i] = (float*)kmalloc(sz[i] * sizeof(float));
        n->a[i] = (float*)kmalloc(sz[i] * sizeof(float));
        n->e[i] = (float*)kmalloc(sz[i] * sizeof(float));
        if (!n->z[i] || !n->a[i] || !n->e[i]) { nnf_free(n); return -1; }
    }
    for (int i = 0; i < nl - 1; i++) {
        n->w[i] = (float*)kmalloc(sz[i + 1] * sz[i] * sizeof(float));
        n->b[i] = (float*)kmalloc(sz[i + 1] * sizeof(float));
        n->dw[i] = (float*)kmalloc(sz[i + 1] * sz[i] * sizeof(float));
        n->db[i] = (float*)kmalloc(sz[i + 1] * sizeof(float));
        if (!n->w[i] || !n->b[i] || !n->dw[i] || !n->db[i]) { nnf_free(n); return -1; }
    }
    n->init = 1;
    return 0;
}

void nnf_free(nn_float_t *n) {
    if (!n || !n->init) return;
    for (int i = 0; i < n->nl; i++) {
        kfree(n->z[i]); kfree(n->a[i]); kfree(n->e[i]);
        n->z[i] = 0; n->a[i] = 0; n->e[i] = 0;
    }
    for (int i = 0; i < n->nl - 1; i++) {
        kfree(n->w[i]); kfree(n->b[i]); kfree(n->dw[i]); kfree(n->db[i]);
        n->w[i] = 0; n->b[i] = 0; n->dw[i] = 0; n->db[i] = 0;
    }
    n->init = 0;
}

void nnf_rand(nn_float_t *n, float scale, int seed) {
    for (int l = 0; l < n->nl - 1; l++) {
        int nw = n->sz[l + 1] * n->sz[l];
        for (int i = 0; i < nw; i++) {
            unsigned v = ((unsigned)(i + seed) * 1103515245u + (unsigned)l * 987654u + 12345u) & 0x7FFFFFFFu;
            n->w[l][i] = ((int)v % 2001 - 1000) / 1000.0f * scale;
        }
        for (int i = 0; i < n->sz[l + 1]; i++) {
            unsigned v = ((unsigned)(i + l + seed + 7) * 1103515245u + 54321u) & 0x7FFFFFFFu;
            n->b[l][i] = ((int)(v % 2001) - 1000) / 1000.0f * scale;
        }
    }
}

void nnf_fwd(nn_float_t *n, float *in) {
    int r;
    for (int i = 0; i < n->sz[0]; i++) n->a[0][i] = in[i];
    for (int l = 0; l < n->nl - 1; l++) {
        int cr = n->sz[l + 1], cc = n->sz[l];
        r = 0;
        if (kpu_available() && cr >= 2 && cc >= 2) {
            float tmp_small[64], *tmp = tmp_small;
            int alloced = 0;
            if (cr > 64) { tmp = (float*)kmalloc(cr * sizeof(float)); alloced = 1; }
            if (tmp) {
                kpu_mat_mul_float(tmp, n->w[l], n->a[l], cr, cc, 1);
                for (int i = 0; i < cr; i++) n->z[l + 1][i] = tmp[i] + n->b[l][i];
                if (alloced) kfree(tmp);
                r = 1;
            }
        }
        if (!r) {
            for (int i = 0; i < cr; i++) {
                float sum = n->b[l][i];
                for (int j = 0; j < cc; j++)
                    sum += n->w[l][i * cc + j] * n->a[l][j];
                n->z[l + 1][i] = sum;
            }
        }
        for (int i = 0; i < cr; i++) n->a[l + 1][i] = n->z[l + 1][i];
        sigmoid_a(n->a[l + 1], cr);
    }
}

float nnf_bwd(nn_float_t *n, float *targ, float lr) {
    int L = n->nl - 1, cr, cc;
    for (int i = 0; i < n->sz[L]; i++) n->e[L][i] = n->a[L][i] - targ[i];
    float loss = 0;
    for (int i = 0; i < n->sz[L]; i++) loss += n->e[L][i] * n->e[L][i];
    for (int l = L; l >= 1; l--) {
        cr = n->sz[l];
        for (int i = 0; i < cr; i++)
            n->e[l][i] *= n->a[l][i] * (1.0f - n->a[l][i]);
        cc = n->sz[l - 1];
        int r = 0;
        if (kpu_available() && cr >= 2 && cc >= 2) {
            kpu_mat_mul_float(n->dw[l - 1], n->e[l], n->a[l - 1], cr, 1, cc);
            r = 1;
        }
        if (!r) {
            for (int i = 0; i < cr; i++)
                for (int j = 0; j < cc; j++)
                    n->dw[l - 1][i * cc + j] = n->e[l][i] * n->a[l - 1][j];
        }
        for (int i = 0; i < cr; i++) n->db[l - 1][i] = n->e[l][i];
        if (l > 1) {
            int prev = n->sz[l - 1];
            for (int i = 0; i < prev; i++) {
                float sum = 0;
                for (int j = 0; j < cr; j++)
                    sum += n->w[l - 1][j * prev + i] * n->e[l][j];
                n->e[l - 1][i] = sum;
            }
        }
    }
    for (int l = 0; l < L; l++) {
        int nw = n->sz[l + 1] * n->sz[l];
        for (int i = 0; i < nw; i++) n->w[l][i] -= n->dw[l][i] * lr;
        for (int i = 0; i < n->sz[l + 1]; i++) n->b[l][i] -= n->db[l][i] * lr;
    }
    return loss;
}

int nnf_infer(nn_float_t *n, float *in, float *out) {
    if (!n->init) return -1;
    nnf_fwd(n, in);
    int ol = n->sz[n->nl - 1];
    for (int i = 0; i < ol; i++) out[i] = n->a[n->nl - 1][i];
    return 0;
}

int nnf_save(nn_float_t *n, const char *path) {
    int hdr = 4 + n->nl * 4;
    int wtotal = 0, btotal = 0;
    for (int l = 0; l < n->nl - 1; l++) { wtotal += n->sz[l + 1] * n->sz[l]; }
    for (int l = 0; l < n->nl - 1; l++) { btotal += n->sz[l + 1]; }
    if (wtotal > 262144 / 4 || btotal > 262144 / 4) return -1;
    int total = hdr + wtotal * 4 + btotal * 4;
    unsigned char *buf = (unsigned char*)kmalloc(total);
    if (!buf) return -1;
    unsigned char *p = buf;
    *(int*)p = n->nl; p += 4;
    for (int i = 0; i < n->nl; i++) { *(int*)p = n->sz[i]; p += 4; }
    for (int l = 0; l < n->nl - 1; l++) {
        int nw = n->sz[l + 1] * n->sz[l];
        for (int i = 0; i < nw; i++) { *(float*)p = n->w[l][i]; p += 4; }
    }
    for (int l = 0; l < n->nl - 1; l++) {
        for (int i = 0; i < n->sz[l + 1]; i++) { *(float*)p = n->b[l][i]; p += 4; }
    }
    int r = fs_write(path, buf, total);
    kfree(buf);
    return r == total ? 0 : -1;
}

int nnf_load(nn_float_t *n, const char *path) {
    unsigned char hdr[64];
    int nread = fs_read(path, hdr, 64);
    if (nread < 4) return -1;
    int nl = *(int*)hdr;
    if (nl <= 0 || nl > NNF_MAX_L) return -1;
    int hdr_sz = 4 + nl * 4;
    if (nread < hdr_sz) return -1;
    int sz[NNF_MAX_L];
    for (int i = 0; i < nl; i++) sz[i] = ((int*)hdr)[i + 1];
    int wtotal = 0, btotal = 0;
    for (int l = 0; l < nl - 1; l++) { wtotal += sz[l + 1] * sz[l]; }
    for (int l = 0; l < nl - 1; l++) { btotal += sz[l + 1]; }
    if (wtotal > 262144 / 4 || btotal > 262144 / 4) return -1;
    int total = hdr_sz + wtotal * 4 + btotal * 4;
    unsigned char *buf = (unsigned char*)kmalloc(total);
    if (!buf) return -1;
    int nread2 = fs_read(path, buf, total);
    if (nread2 < total) { kfree(buf); return -1; }
    if (nnf_init(n, nl, sz)) { kfree(buf); return -1; }
    unsigned char *p = buf + hdr_sz;
    for (int l = 0; l < n->nl - 1; l++) {
        int nw = n->sz[l + 1] * n->sz[l];
        for (int i = 0; i < nw; i++) { n->w[l][i] = *(float*)p; p += 4; }
    }
    for (int l = 0; l < n->nl - 1; l++) {
        for (int i = 0; i < n->sz[l + 1]; i++) { n->b[l][i] = *(float*)p; p += 4; }
    }
    kfree(buf);
    return 0;
}
