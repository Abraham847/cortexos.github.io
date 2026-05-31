#include "ipc.h"
#include "task.h"

static ipc_mbox_t mboxes[MAX_TASKS];

void ipc_init(void) {
    for (int i = 0; i < MAX_TASKS; i++) {
        mboxes[i].head = 0;
        mboxes[i].tail = 0;
        mboxes[i].count = 0;
    }
}

int ipc_send(int dst_tid, int type, const char *data, int len) {
    if (dst_tid < 0 || dst_tid >= MAX_TASKS) return -1;
    ipc_mbox_t *mb = &mboxes[dst_tid];
    if (mb->count >= IPC_MBOX_SZ) return -1;
    ipc_msg_t *m = &mb->msgs[mb->tail];
    m->src = current_task_id();
    m->type = type;
    int i;
    for (i = 0; i < len && i < IPC_DATA_SZ; i++) m->data[i] = data[i];
    m->len = i;
    mb->tail = (mb->tail + 1) % IPC_MBOX_SZ;
    mb->count++;
    return 0;
}

int ipc_recv(int src_filter, int *type, char *data, int *len) {
    int self = current_task_id();
    if (self < 0 || self >= MAX_TASKS) return -1;
    ipc_mbox_t *mb = &mboxes[self];
    if (mb->count == 0) return -1;
    for (int i = 0; i < mb->count; i++) {
        int idx = (mb->head + i) % IPC_MBOX_SZ;
        ipc_msg_t *m = &mb->msgs[idx];
        if (src_filter < 0 || m->src == src_filter) {
            if (type) *type = m->type;
            if (data && m->len > 0) {
                int j;
                for (j = 0; j < m->len && j < IPC_DATA_SZ; j++) data[j] = m->data[j];
                if (len) *len = j;
            } else if (len) *len = 0;
            for (int j = i; j < mb->count - 1; j++) {
                int ri = (mb->head + j) % IPC_MBOX_SZ;
                int wi = (mb->head + j) % IPC_MBOX_SZ;
                mb->msgs[wi] = mb->msgs[(ri + 1) % IPC_MBOX_SZ];
            }
            mb->tail = (mb->tail - 1 + IPC_MBOX_SZ) % IPC_MBOX_SZ;
            mb->count--;
            return m->src;
        }
    }
    return -1;
}

int ipc_available(int src_filter) {
    int self = current_task_id();
    if (self < 0 || self >= MAX_TASKS) return 0;
    ipc_mbox_t *mb = &mboxes[self];
    if (src_filter < 0) return mb->count;
    int n = 0;
    for (int i = 0; i < mb->count; i++) {
        int idx = (mb->head + i) % IPC_MBOX_SZ;
        if (mb->msgs[idx].src == src_filter) n++;
    }
    return n;
}

int ipc_self(void) {
    return current_task_id();
}
