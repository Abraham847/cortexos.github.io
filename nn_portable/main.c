#include "nn.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    printf("CortexOS Neural Network - Portable Demo\n");
    printf("========================================\n\n");

    int sz[] = {2, 6, 1};
    nn net;
    nn_init(&net, 3, sz);
    nn_rand(&net, FF(2));
    net.acts[1] = ACT_RELU;
    printf("Network: 2 -> 6 (relu) -> 1 (sigmoid)\n\n");

    dataset ds;
    if (ds_load(&ds, "xordset.bin") == 0) {
        printf("Loaded XOR dataset: %d samples (2 in, 1 out)\n\n", ds.n);
    } else {
        printf("No xordset.bin found, using inline XOR data\n");
        fp in_data[]  = { FF(0), FF(0), FF(0), FF(1), FF(1), FF(0), FF(1), FF(1) };
        fp out_data[] = { FF(0), FF(1), FF(1), FF(0) };
        ds.n = 4; ds.ni = 2; ds.no = 1;
        ds.in  = in_data;
        ds.out = out_data;
    }

    printf("Before training:\n");
    for (int i = 0; i < ds.n; i++) {
        fp out[1];
        nn_fwd(&net, ds.in + i * ds.ni);
        out[0] = net.a[net.nl - 1].d[0];
        printf("  %d ^ %d = %d%%\n",
            FI(ds.in[i * ds.ni]),
            FI(ds.in[i * ds.ni + 1]),
            FI(out[0] * 100));
    }

    printf("\nTraining 2000 epochs, LR=0.5...\n");
    fp lr = FF(0.5);
    for (int e = 0; e < 2000; e++) {
        fp loss = ds_train_epoch(&net, &ds, lr);
        if ((e + 1) % 500 == 0)
            printf("  epoch %d, loss = %d%%\n", e + 1, FI(loss * 100 / ds.n));
    }

    printf("\nAfter training:\n");
    nn_infer(&net, ds.in, ds.out);
    for (int i = 0; i < ds.n; i++) {
        fp out[1];
        nn_fwd(&net, ds.in + i * ds.ni);
        out[0] = net.a[net.nl - 1].d[0];
        printf("  %d ^ %d = %d%%  (expected %d)\n",
            FI(ds.in[i * ds.ni]),
            FI(ds.in[i * ds.ni + 1]),
            FI(out[0] * 100),
            FI(ds.out[i * ds.no]));
    }

    nn_save_file(&net, "trained_xor.bin");
    printf("\nModel saved to trained_xor.bin\n");

    char txtbuf[4096];
    int n = nn_export_txt(&net, txtbuf, 4096);
    if (n > 0) {
        FILE *f = fopen("trained_xor.txt", "w");
        fwrite(txtbuf, 1, n, f);
        fclose(f);
        printf("Text export saved to trained_xor.txt\n");
    }

    nn_free(&net);
    printf("\nDone.\n");
    return 0;
}
