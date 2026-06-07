#ifndef NN_H
#define NN_H

/* no kernel.h dependency needed */

#define NN_MAX_L 8

typedef int fp;
#define FPS 16
#define F1 (1 << FPS)

static inline fp fpm(fp a, fp b) { return (fp)(((long long)a * b) >> FPS); }
fp fpd(fp a, fp b);

#define FI(a) ((a) >> FPS)
#define FF(a) ((fp)((a) * F1))

typedef enum { ACT_SIGMOID = 0, ACT_RELU, ACT_TANH, ACT_LEAKY_RELU } nn_act_t;

typedef struct { int r, c; fp *d; } mat;

typedef struct nn_s {
    int nl;
    int sz[NN_MAX_L];
    nn_act_t acts[NN_MAX_L];
    mat w[NN_MAX_L - 1];
    mat b[NN_MAX_L - 1];
    mat z[NN_MAX_L];
    mat a[NN_MAX_L];
    mat dw[NN_MAX_L - 1];
    mat db[NN_MAX_L - 1];
    mat e[NN_MAX_L];
    int init;
} nn;

int nn_init(nn *n, int nl, int *sz);
void nn_free(nn *n);
void nn_rand(nn *n, fp s);
void nn_fwd(nn *n, fp *in);
fp nn_bwd(nn *n, fp *targ, fp lr);
/* use nn_save_file/nn_load_file instead (raw ATA versions removed) */
int nn_save_file(nn *n, const char *path);
int nn_load_file(nn *n, const char *path);
int nn_export_txt(nn *n, char *buf, int max);
int nn_export_csv_file(nn *n, const char *path);
const char *nn_act_name(nn_act_t act);

typedef struct { int n, ni, no; fp *in, *out; } dataset;

int ds_load(dataset *ds, const char *path);
int ds_train_epoch(nn *n, dataset *ds, fp lr);
int ds_export_text(dataset *ds, const char *path);
int ds_import_text(dataset *ds, const char *path, int ni, int no);
int nn_infer(nn *n, fp *in, fp *out);

#endif
