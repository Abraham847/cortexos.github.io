#ifndef TCP_H
#define TCP_H

#include "core.h"
#include "net.h"

#define TCP_FLAG_FIN 1
#define TCP_FLAG_SYN 2
#define TCP_FLAG_RST 4
#define TCP_FLAG_PSH 8
#define TCP_FLAG_ACK 16

#pragma pack(push, 1)
typedef struct {
    u16 sport, dport;
    u32 seq, ack;
    u8 off_flags;
    u8 flags;
    u16 window, chksum, urg;
} tcp_hdr_t;
#pragma pack(pop)

void tcp_init(void);
int tcp_connect(ip4_t ip, u16 port);
int tcp_send(int sock, const u8 *data, int len);
int tcp_recv(int sock, u8 *buf, int max);
int tcp_close(int sock);

#endif
