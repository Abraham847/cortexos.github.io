#include "dns.h"
#include "rtl8139.h"
#include "heap.h"

#define DNS_PORT 53

/* Build a DNS query name from a hostname like "www.google.com" */
static int dns_encode_name(u8 *buf, const char *name) {
    int pos = 0;
    while (*name) {
        const char *dot = name;
        while (*dot && *dot != '.') dot++;
        int len = dot - name;
        buf[pos++] = len;
        for (int i = 0; i < len; i++) buf[pos++] = name[i];
        name = dot;
        if (*dot == '.') name++;
    }
    buf[pos++] = 0;
    return pos;
}

/* Decode a DNS name from response (handles compression) */
static int dns_decode_name_depth(const u8 *msg, int msglen, int off, char *out, int max, int depth);

static int dns_decode_name(const u8 *msg, int msglen, int off, char *out, int max) {
    return dns_decode_name_depth(msg, msglen, off, out, max, 0);
}

static int dns_decode_name_depth(const u8 *msg, int msglen, int off, char *out, int max, int depth) {
    if (depth > 8) return -1;
    int pos = 0;
    while (off >= 0 && off < msglen) {
        u8 b = msg[off++];
        if (b == 0) break;
        if (b & 0xC0) {
            if (off >= msglen) return -1;
            u16 ptr = ((b & 0x3F) << 8) | msg[off++];
            return dns_decode_name_depth(msg, msglen, ptr, out, max, depth + 1);
        }
        if (pos > 0 && pos < max - 1) out[pos++] = '.';
        for (int i = 0; i < b && off < msglen && pos < max - 1; i++)
            out[pos++] = msg[off++];
    }
    out[pos] = 0;
    return off;
}

/* Send UDP packet directly via IP */
static int send_udp(u32 dst_be, u16 sport, u16 dport, const u8 *data, int len) {
    ip4_t dst;
    *(u32*)dst.addr = dst_be;
    u8 mac[6];
    if (!net_arp_resolve(dst, mac)) return -1;
    int udp_len = 8 + len;
    int total = IP_HLEN + udp_len;
    u8 pkt[total];
    ip_hdr_t *ip = (ip_hdr_t*)pkt;
    ip->ver_ihl = IP_VER;
    ip->dscp = 0;
    ip->len = ((total>>8)&0xFF) | ((total&0xFF)<<8);
    ip->id = 0;
    ip->frag = 0;
    ip->ttl = 64;
    ip->proto = 17;
    *(u32*)ip->src = *(u32*)net_my_ip.addr;
    *(u32*)ip->dst = dst_be;
    ip->chksum = 0;
    u32 sum = 0;
    for (int i = 0; i < 10; i++) sum += ((u16*)ip)[i];
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    ip->chksum = ~sum & 0xFFFF;
    u8 *udp = pkt + IP_HLEN;
    udp[0] = sport>>8; udp[1] = sport&0xFF;
    udp[2] = dport>>8; udp[3] = dport&0xFF;
    udp[4] = udp_len>>8; udp[5] = udp_len&0xFF;
    udp[6] = 0; udp[7] = 0;
    for (int i = 0; i < len; i++) udp[8 + i] = data[i];
    return net_send_eth(mac, ETH_TYPE_IP, pkt, total);
}

/* Poll for UDP response */
static int recv_udp(u16 sport, u8 *buf, int max) {
    u8 eth[1518];
    for (int t = 0; t < 300; t++) {
        int n;
        while ((n = rtl8139_recv(eth, 1518)) > 0) {
            if (n < 14 + 20 + 8) continue;
            u16 etype = (eth[12]<<8)|eth[13];
            if (etype != ETH_TYPE_IP) continue;
            const u8 *ip = eth + 14;
            int iplen = ((ip[2]<<8)|ip[3]) - 20;
            if (iplen < 8) continue;
            const u8 *udp = ip + 20;
            u16 dport = (udp[2]<<8)|udp[3];
            if (dport != sport) continue;
            int ulen = (udp[4]<<8)|udp[5];
            int data_len = ulen - 8;
            if (data_len > max) data_len = max;
            for (int i = 0; i < data_len; i++) buf[i] = udp[8 + i];
            return data_len;
        }
    }
    return 0;
}

int dns_resolve(const char *hostname, ip4_t *result) {
    /* Build DNS query */
    u8 query[512];
    int pos = 0;
    /* Header */
    u16 id = (u16)(timer_ticks & 0xFFFF);
    query[pos++] = id>>8; query[pos++] = id&0xFF;
    query[pos++] = 1; query[pos++] = 0;
    query[pos++] = 0; query[pos++] = 1;
    query[pos++] = 0; query[pos++] = 0;
    query[pos++] = 0; query[pos++] = 0;
    query[pos++] = 0; query[pos++] = 0;
    /* Question */
    pos += dns_encode_name(query + pos, hostname);
    query[pos++] = 0; query[pos++] = 1;
    query[pos++] = 0; query[pos++] = 1;

    /* DNS server IP in net byte order */
    u32 dns_be;
    {
        u8 dns_ip[4] = {10, 0, 2, 3};
        dns_be = (dns_ip[0]<<24)|(dns_ip[1]<<16)|(dns_ip[2]<<8)|dns_ip[3];
    }

    u16 sport = 0xC000 | (id & 0x3FFF);
    if (send_udp(dns_be, sport, DNS_PORT, query, pos) < 0) return -1;

    u8 reply[512];
    int rlen = recv_udp(sport, reply, 512);
    if (rlen < 12) return -1;

    /* Parse response header */
    int ancount = (reply[6]<<8)|reply[7];
    if (ancount == 0) return -1;

    /* Skip question */
    int off = 12;
    char tmp[256];
    off = dns_decode_name(reply, rlen, off, tmp, 256);
    off += 4;

    /* Parse answers */
    for (int a = 0; a < ancount; a++) {
        off = dns_decode_name(reply, rlen, off, tmp, 256);
        if (off + 10 > rlen) return -1;
        u16 type = (reply[off]<<8)|reply[off+1];
        u16 rdlen = (reply[off+8]<<8)|reply[off+9];
        off += 10;
        if (type == 1 && rdlen == 4 && off + 4 <= rlen) {
            result->addr[0] = reply[off];
            result->addr[1] = reply[off+1];
            result->addr[2] = reply[off+2];
            result->addr[3] = reply[off+3];
            return 0;
        }
        off += rdlen;
    }
    return -1;
}
