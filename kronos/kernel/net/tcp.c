#include "tcp.h"
#include "rtl8139.h"
#include "heap.h"

#define TCP_MAX_SOCK 4
#define TCP_WINDOW 1460
#define TCP_MSS 1460

typedef struct {
    u32 s_addr;
} ip4_net_t;

#pragma pack(push, 1)
typedef struct {
    ip4_net_t src, dst;
    u8 zero, proto;
    u16 len;
} tcp_pseudo_t;
#pragma pack(pop)

typedef struct {
    int used;
    u32 seq, ack;
    u16 sport, dport;
    u32 ip_be;
    u8 mac[6];
    int state;
    u8 rx_buf[2048];
    int rx_len;
} tcp_sock_t;

static tcp_sock_t socks[TCP_MAX_SOCK];
static u32 tcp_seq_base;

#define BE16(x) ((((x)>>8)&0xFF)|(((x)&0xFF)<<8))
#define BE32(x) ((((x)>>24)&0xFF)|(((x)>>8)&0xFF00)|(((x)&0xFF00)<<8)|(((x)&0xFF)<<24))

static u16 tcp_chksum(const u8 *src_ip, const u8 *dst_ip, const u8 *tcp_seg, int len) {
    tcp_pseudo_t pseudo;
    pseudo.src.s_addr = *(const u32*)src_ip;
    pseudo.dst.s_addr = *(const u32*)dst_ip;
    pseudo.zero = 0;
    pseudo.proto = IP_PROTO_TCP;
    pseudo.len = BE16(len);
    u32 sum = 0;
    u16 *p = (u16*)&pseudo;
    for (int i = 0; i < 6; i++) sum += p[i];
    p = (u16*)tcp_seg;
    for (int i = 0; i < len / 2; i++) sum += p[i];
    if (len & 1) sum += ((u8*)tcp_seg)[len - 1];
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return ~sum & 0xFFFF;
}

static int send_ip(u32 dst_be, u8 proto, const u8 *data, int len) {
    ip4_t ip;
    *(u32*)ip.addr = dst_be;
    u8 mac[6];
    if (!net_arp_resolve(ip, mac)) return -1;
    u8 pkt[IP_HLEN + len];
    ip_hdr_t *ip_hdr = (ip_hdr_t*)pkt;
    ip_hdr->ver_ihl = IP_VER;
    ip_hdr->dscp = 0;
    ip_hdr->len = BE16(IP_HLEN + len);
    ip_hdr->id = BE16((u16)(timer_ticks & 0xFFFF));
    ip_hdr->frag = 0;
    ip_hdr->ttl = 64;
    ip_hdr->proto = proto;
    *(u32*)ip_hdr->src = *(u32*)net_my_ip.addr;
    *(u32*)ip_hdr->dst = dst_be;
    ip_hdr->chksum = 0;
    u32 sum = 0;
    for (int i = 0; i < IP_HLEN / 2; i++) sum += ((u16*)ip_hdr)[i];
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    ip_hdr->chksum = ~sum & 0xFFFF;
    for (int i = 0; i < len; i++) pkt[IP_HLEN + i] = data[i];
    return net_send_eth(mac, ETH_TYPE_IP, pkt, IP_HLEN + len);
}

static int send_tcp(tcp_sock_t *sock, u8 flags, const u8 *data, int len) {
    int hdr_len = 20;
    int total = hdr_len + len;
    u8 seg[total];
    tcp_hdr_t *tcp = (tcp_hdr_t*)seg;
    tcp->sport = BE16(sock->sport);
    tcp->dport = BE16(sock->dport);
    tcp->seq = BE32(sock->seq);
    tcp->ack = BE32(sock->ack);
    tcp->off_flags = (hdr_len / 4) << 4;
    tcp->flags = flags;
    tcp->window = BE16(TCP_WINDOW);
    tcp->urg = 0;
    tcp->chksum = 0;
    for (int i = 0; i < len; i++) seg[hdr_len + i] = data[i];
    tcp->chksum = tcp_chksum(net_my_ip.addr, (u8*)&sock->ip_be, seg, total);
    if (flags & TCP_FLAG_SYN) sock->seq++;
    if (flags & TCP_FLAG_FIN) sock->seq++;
    if (len > 0) sock->seq += len;
    return send_ip(sock->ip_be, IP_PROTO_TCP, seg, total);
}

void tcp_init(void) {
    tcp_seq_base = 0x10000000;
    for (int i = 0; i < TCP_MAX_SOCK; i++) socks[i].used = 0;
}

static void tcp_poll(void) {
    u8 buf[ETH_MTU];
    int n;
    while ((n = rtl8139_recv(buf, ETH_MTU)) > 0) {
        if (n < ETH_HLEN + IP_HLEN + 20) continue;
        u16 type = (buf[12]<<8)|buf[13];
        if (type != ETH_TYPE_IP) continue;
        const u8 *ip_raw = buf + ETH_HLEN;
        int iplen = n - ETH_HLEN;
        if (iplen < IP_HLEN) continue;
        ip_hdr_t *ip_hdr = (ip_hdr_t*)ip_raw;
        if (ip_hdr->proto != IP_PROTO_TCP) continue;
        int hdr_len = (ip_hdr->ver_ihl & 0x0F) * 4;
        int ip_total = BE16(ip_hdr->len);
        if (ip_total < hdr_len || ip_total > iplen) continue;
        int tcp_total = ip_total - hdr_len;
        const u8 *tcp_raw = ip_raw + hdr_len;
        if (tcp_total < 20) continue;

        u16 sport = BE16(*(u16*)(tcp_raw));
        u16 dport = BE16(*(u16*)(tcp_raw+2));
        u32 seq = BE32(*(u32*)(tcp_raw+4));
        u32 ack = BE32(*(u32*)(tcp_raw+8));
        u8 flags = tcp_raw[13];

        for (int si = 0; si < TCP_MAX_SOCK; si++) {
            tcp_sock_t *s = &socks[si];
            if (!s->used) continue;
            if (dport != s->sport || sport != s->dport) continue;

            if (s->state == 1 && flags == (TCP_FLAG_SYN|TCP_FLAG_ACK)) {
                s->ack = seq + 1;
                s->seq = ack;
                send_tcp(s, TCP_FLAG_ACK, 0, 0);
                s->state = 2;
                return;
            }

            if (s->state == 2 && (flags & TCP_FLAG_ACK)) {
                int tcp_hdr_len = (tcp_raw[12] >> 4) * 4;
                if (tcp_hdr_len < 20 || tcp_hdr_len > tcp_total) { s->used = 0; return; }
                int data_len = tcp_total - tcp_hdr_len;
                const u8 *payload = tcp_raw + tcp_hdr_len;

                if (data_len > 0 && seq == s->ack) {
                    int copy = data_len;
                    if (s->rx_len + copy > (int)sizeof(s->rx_buf))
                        copy = sizeof(s->rx_buf) - s->rx_len;
                    for (int i = 0; i < copy; i++) s->rx_buf[s->rx_len + i] = payload[i];
                    s->rx_len += copy;
                    s->ack += data_len;
                    send_tcp(s, TCP_FLAG_ACK, 0, 0);
                }

                if (flags & TCP_FLAG_FIN) {
                    if (data_len >= 0) s->ack = seq + data_len + 1;
                    send_tcp(s, TCP_FLAG_ACK, 0, 0);
                    s->state = 0;
                }
                return;
            }
        }
    }
}

int tcp_connect(ip4_t ip, u16 port) {
    int si;
    tcp_sock_t *s = 0;
    for (si = 0; si < TCP_MAX_SOCK; si++) {
        if (!socks[si].used) { s = &socks[si]; break; }
    }
    if (!s) return -1;

    s->used = 1;
    s->ip_be = *(u32*)ip.addr;
    s->sport = 0xC000 + ((u16)(timer_ticks & 0x3FFF));
    s->dport = port;
    s->seq = tcp_seq_base++;
    s->ack = 0;
    s->state = 1;
    s->rx_len = 0;

    if (!net_arp_resolve(ip, s->mac)) { s->used = 0; return -1; }
    send_tcp(s, TCP_FLAG_SYN, 0, 0);

    for (int t = 0; t < 300; t++) {
        tcp_poll();
        if (s->state == 2) return si;
    }

    s->used = 0;
    return -1;
}

int tcp_send(int sock_id, const u8 *data, int len) {
    if (sock_id < 0 || sock_id >= TCP_MAX_SOCK) return -1;
    tcp_sock_t *s = &socks[sock_id];
    if (!s->used || s->state != 2) return -1;
    if (len > TCP_MSS) len = TCP_MSS;
    if (send_tcp(s, TCP_FLAG_ACK | TCP_FLAG_PSH, data, len) < 0) return -1;
    for (int t = 0; t < 100; t++) tcp_poll();
    return len;
}

int tcp_recv(int sock_id, u8 *buf, int max) {
    if (sock_id < 0 || sock_id >= TCP_MAX_SOCK) return -1;
    tcp_sock_t *s = &socks[sock_id];
    if (!s->used) return -1;
    for (int t = 0; t < 300; t++) {
        tcp_poll();
        if (s->rx_len > 0) {
            int n = s->rx_len;
            if (n > max) n = max;
            for (int i = 0; i < n; i++) buf[i] = s->rx_buf[i];
            for (int i = 0; i < s->rx_len - n; i++) s->rx_buf[i] = s->rx_buf[i + n];
            s->rx_len -= n;
            return n;
        }
    }
    return 0;
}

int tcp_close(int sock_id) {
    if (sock_id < 0 || sock_id >= TCP_MAX_SOCK) return -1;
    tcp_sock_t *s = &socks[sock_id];
    if (!s->used) return -1;
    s->state = 3;
    send_tcp(s, TCP_FLAG_FIN | TCP_FLAG_ACK, 0, 0);
    for (int t = 0; t < 100; t++) tcp_poll();
    s->used = 0;
    return 0;
}
