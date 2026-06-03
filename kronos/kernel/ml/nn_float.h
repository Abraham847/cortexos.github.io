#ifndef NN_FLOAT_H
#define NN_FLOAT_H

#define NNF_MAX_L 8

typedef struct {
    int nl;
    int sz[NNF_MAX_L];
    float *w[NNF_MAX_L];
    float *b[NNF_MAX_L];
    float *z[NNF_MAX_L];
    float *a[NNF_MAX_L];
    float *e[NNF_MAX_L];
    float *dw[NNF_MAX_L];
    float *db[NNF_MAX_L];
    int init;
} nn_float_t;

int nnf_init(nn_float_t *n, int nl, int *sz);
void nnf_free(nn_float_t *n);
void nnf_rand(nn_float_t *n, float s, int seed);
void nnf_fwd(nn_float_t *n, float *in);
float nnf_bwd(nn_float_t *n, float *targ, float lr);
int nnf_infer(nn_float_t *n, float *in, float *out);
int nnf_save(nn_float_t *n, const char *path);
int nnf_load(nn_float_t *n, const char *path);

#endif
