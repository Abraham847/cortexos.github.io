#include "kpu.h"
#include "core.h"

static int fpu_enabled;

int kpu_available(void) { return fpu_enabled; }

int kpu_init(void) {
    /* Check if CPU has FPU via CR0 */
    u32 cr0, cr4;

    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));

    /* Clear EM (bit 2), set MP (bit 1), set NE (bit 5) */
    cr0 &= ~0x04;
    cr0 |= 0x22;
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));

    /* Enable FXSAVE/FXRSTOR and SSE exceptions */
    cr4 |= 0x600;
    __asm__ volatile("mov %0, %%cr4" : : "r"(cr4));

    /* Initialize FPU */
    __asm__ volatile("fninit");

    fpu_enabled = 1;
    return 1;
}

void kpu_mat_mul_float(float *dst, float *src, float *weights, int r, int k, int c) {
    if (!fpu_enabled) return;
    for (int i = 0; i < r; i++) {
        for (int j = 0; j < c; j++) {
            float sum = 0;
            for (int t = 0; t < k; t++) {
                sum += src[i * k + t] * weights[t * c + j];
            }
            dst[i * c + j] = sum;
        }
    }
}

void kpu_mat_add_float(float *dst, float *src, int n) {
    if (!fpu_enabled) return;
    for (int i = 0; i < n; i++) dst[i] += src[i];
}

static float sigmoid_lut[33];

static void init_lut(void) {
    static int done;
    if (done) return;
    done = 1;
    for (int i = 0; i < 33; i++) {
        float x = -8.0f + i * 0.5f;
        float ex = 1.0f;
        float term = 1.0f;
        for (int k = 1; k < 20; k++) {
            term *= x / k;
            ex += term;
        }
        sigmoid_lut[i] = 1.0f / (1.0f + 1.0f / ex);
    }
}

void kpu_sigmoid_float(float *v, int n) {
    if (!fpu_enabled) return;
    init_lut();
    for (int i = 0; i < n; i++) {
        float x = v[i];
        if (x <= -8.0f) v[i] = 0.0f;
        else if (x >= 8.0f) v[i] = 1.0f;
        else {
            int idx = (int)((x + 8.0f) * 2.0f);
            if (idx < 0) idx = 0;
            if (idx > 31) idx = 31;
            float frac = (x + 8.0f) * 2.0f - idx;
            v[i] = sigmoid_lut[idx] + (sigmoid_lut[idx + 1] - sigmoid_lut[idx]) * frac;
        }
    }
}

void kpu_relu_float(float *v, int n) {
    if (!fpu_enabled) return;
    for (int i = 0; i < n; i++)
        if (v[i] < 0) v[i] = 0;
}

void kpu_fpu_save(void *state) {
    if (!fpu_enabled) return;
    __asm__ volatile("fnsave (%0)" : : "r"(state) : "memory");
}

void kpu_fpu_restore(void *state) {
    if (!fpu_enabled) return;
    __asm__ volatile("frstor (%0)" : : "r"(state) : "memory");
}
