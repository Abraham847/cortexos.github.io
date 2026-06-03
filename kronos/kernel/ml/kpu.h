#ifndef KPU_H
#define KPU_H

/* Kernel Processing Unit - FPU-accelerated math for NN */

/* Returns 1 if FPU was detected and enabled */
int kpu_init(void);
int kpu_available(void);

/* Matrix multiply: dst(rxc) = src(rxk) * weights(kxc) (float) */
void kpu_mat_mul_float(float *dst, float *src, float *weights, int r, int k, int c);

/* Matrix add: dst += src */
void kpu_mat_add_float(float *dst, float *src, int n);

/* Apply sigmoid activation in-place */
void kpu_sigmoid_float(float *v, int n);

/* Apply ReLU activation in-place */
void kpu_relu_float(float *v, int n);

/* FPU state save/restore for context switching */
void kpu_fpu_save(void *state);
void kpu_fpu_restore(void *state);

#endif
