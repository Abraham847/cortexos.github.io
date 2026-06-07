#ifndef IPC_H
#define IPC_H

#include "core.h"

#define IPC_MBOX_SZ 8
#define IPC_DATA_SZ 24

typedef struct {
    int src;
    int type;
    char data[IPC_DATA_SZ];
    int len;
} ipc_msg_t;

typedef struct {
    ipc_msg_t msgs[IPC_MBOX_SZ];
    int head, tail, count;
} ipc_mbox_t;

void ipc_init(void);
int ipc_send(int dst_tid, int type, const char *data, int len);
int ipc_recv(int src_filter, int *type, char *data, int *len);
int ipc_available(int src_filter);
int ipc_self(void);

#endif
