#include "kernel.h"
#include "nn.h"
#include "heap.h"
#include "model.h"
#include "ipc.h"
#include "task.h"

static nn *shared_net;
static int created;

void aidemo_init(void) {
    int sz[] = {2, 6, 1};
    nn *n = (nn*)kmalloc(sizeof(nn));
    if (!n) return;
    if (nn_init(n, 3, sz)) { kfree(n); return; }
    nn_rand(n, FF(2));
    n->acts[1] = ACT_RELU;
    n->acts[2] = ACT_SIGMOID;
    if (model_register("demo", n) == 0) {
        created = 1;
    } else {
        nn_free(n); kfree(n);
    }
}

void aidemo_task(void) {
    shared_net = model_get("demo");
    if (!shared_net) return;

    fp in[2];
    fp out[1];
    int step = 0;

    while (1) {
        in[0] = FF(step % 2);
        in[1] = FF((step / 2) % 2);
        nn_infer(shared_net, in, out);

        char msg[IPC_DATA_SZ];
        int pos = 0;
        char tmp[16];
        msg[pos++] = 'X'; msg[pos++] = 'O'; msg[pos++] = 'R'; msg[pos++] = ':';
        itoa(FI(in[0]), tmp);
        for (int i = 0; tmp[i] && pos < IPC_DATA_SZ - 1; i++) msg[pos++] = tmp[i];
        msg[pos++] = '^';
        itoa(FI(in[1]), tmp);
        for (int i = 0; tmp[i] && pos < IPC_DATA_SZ - 1; i++) msg[pos++] = tmp[i];
        msg[pos++] = '=';
        int ov = FI(out[0] * 100);
        itoa(ov, tmp);
        for (int i = 0; tmp[i] && pos < IPC_DATA_SZ - 1; i++) msg[pos++] = tmp[i];
        msg[pos] = 0;

        ipc_send(0, 2, msg, pos);
        model_put("demo");
        shared_net = model_get("demo");
        step++;
        for (int i = 0; i < 50; i++) task_yield();
    }
}
