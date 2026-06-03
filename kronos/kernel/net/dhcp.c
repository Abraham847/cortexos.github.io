#include "dhcp.h"
#include "net.h"
#include "rtl8139.h"

#define DHCP_SERVER_PORT 67
#define DHCP_CLIENT_PORT 68

#define DHCP_DISCOVER 1
#define DHCP_OFFER    2
#define DHCP_REQUEST  3
#define DHCP_ACK      5

#define DHCP_MAGIC 0x63825363

/* Send UDP packet manually (simplified) */
static int send_udp_bc(u16 sport, u16 dport, const u8 *data, int len) {
    u8 bc_mac[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    int udp_len = 8 + len;
    int total = 20 + udp_len;
    u8 pkt[total];
    ip_hdr_t *ip = (ip_hdr_t*)pkt;
    ip->ver_ihl = 0x45;
    ip->dscp = 0;
    ip->len = ((total>>8)&0xFF)|((total&0xFF)<<8);
    ip->id = 0;
    ip->frag = 0;
    ip->ttl = 64;
    ip->proto = 17;
    *(u32*)ip->src = 0;
    *(u32*)ip->dst = 0xFFFFFFFF;
    ip->chksum = 0;
    u32 sum = 0;
    for (int i = 0; i < 10; i++) sum += ((u16*)ip)[i];
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    ip->chksum = ~sum & 0xFFFF;
    u8 *udp = pkt + 20;
    udp[0] = sport>>8; udp[1] = sport&0xFF;
    udp[2] = dport>>8; udp[3] = dport&0xFF;
    udp[4] = udp_len>>8; udp[5] = udp_len&0xFF;
    udp[6] = 0; udp[7] = 0;
    for (int i = 0; i < len; i++) udp[8 + i] = data[i];
    return net_send_eth(bc_mac, ETH_TYPE_IP, pkt, total);
}

/* Poll for DHCP response */
static int recv_udp_port(u16 port, u8 *buf, int max) {
    u8 eth[1518];
    for (int t = 0; t < 500; t++) {
        int n;
        while ((n = rtl8139_recv(eth, 1518)) > 0) {
            if (n < 14 + 20 + 8) continue;
            u16 etype = (eth[12]<<8)|eth[13];
            if (etype != ETH_TYPE_IP) continue;
            const u8 *ip_raw = eth + 14;
            const u8 *udp_raw = ip_raw + 20;
            u16 dport = (udp_raw[2]<<8)|udp_raw[3];
            if (dport != port) continue;
            int ulen = (udp_raw[4]<<8)|udp_raw[5];
            int data_len = ulen - 8;
            if (data_len > max) data_len = max;
            for (int i = 0; i < data_len; i++) buf[i] = udp_raw[8 + i];
            return data_len;
        }
    }
    return 0;
}

int dhcp_request(void) {
    /* Build DHCP DISCOVER */
    u8 pkt[300];
    int pos = 0;

    u16 xid = (u16)(timer_ticks & 0xFFFF);

    /* BOOTP header (RFC 951) */
    pkt[pos++] = 1;              /* op = BOOTREQUEST */
    pkt[pos++] = 1;              /* htype = Ethernet */
    pkt[pos++] = 6;              /* hlen = 6 */
    pkt[pos++] = 0;              /* hops = 0 */
    pkt[pos++] = xid>>8; pkt[pos++] = xid&0xFF;  /* xid (bytes 4-5) */
    pkt[pos++] = 0; pkt[pos++] = 0;              /* xid (bytes 6-7) */
    pkt[pos++] = 0; pkt[pos++] = 0;              /* secs = 0 */
    pkt[pos++] = 0; pkt[pos++] = 0;              /* flags = 0 */
    pkt[pos++] = 0; pkt[pos++] = 0; pkt[pos++] = 0; pkt[pos++] = 0;  /* ciaddr = 0 */
    pkt[pos++] = 0; pkt[pos++] = 0; pkt[pos++] = 0; pkt[pos++] = 0;  /* yiaddr = 0 */

    for (int i = 0; i < 16; i++) pkt[pos++] = 0;
    for (int i = 0; i < 16; i++) pkt[pos++] = 0;

    for (int i = 0; i < 64; i++) pkt[pos++] = 0;

    for (int i = 0; i < 8; i++) pkt[pos++] = 0;

    /* Magic cookie */
    pkt[pos++] = 99; pkt[pos++] = 130;
    pkt[pos++] = 83; pkt[pos++] = 99;

    /* DHCP discover option */
    pkt[pos++] = 53; pkt[pos++] = 1; pkt[pos++] = DHCP_DISCOVER;
    pkt[pos++] = 55; pkt[pos++] = 2; pkt[pos++] = 1; pkt[pos++] = 3;
    pkt[pos++] = 0xFF;

    send_udp_bc(DHCP_CLIENT_PORT, DHCP_SERVER_PORT, pkt, pos);

    u8 reply[512];
    int rlen = recv_udp_port(DHCP_CLIENT_PORT, reply, 512);
    if (rlen < 240) return -1;

    /* Extract YIADDR (your IP) at offset 16 */
    u8 yiaddr[4] = { reply[16], reply[17], reply[18], reply[19] };

    /* Parse options for message type */
    int op_pos = 240;
    int msg_type = 0;
    u8 server_ip[4] = {0};
    while (op_pos < rlen) {
        if (op_pos >= rlen) break;
        u8 opt = reply[op_pos++];
        if (opt == 0) continue;
        if (opt == 0xFF) break;
        if (op_pos >= rlen) break;
        u8 olen = reply[op_pos++];
        if (op_pos + olen > rlen) break;
        if (opt == 53 && olen == 1) msg_type = reply[op_pos];
        if (opt == 54 && olen == 4) {
            server_ip[0] = reply[op_pos];
            server_ip[1] = reply[op_pos+1];
            server_ip[2] = reply[op_pos+2];
            server_ip[3] = reply[op_pos+3];
        }
        op_pos += olen;
    }

    if (msg_type != DHCP_OFFER) return -1;

    /* Send REQUEST */
    pos = 0;
    pkt[pos++] = 1;              /* op = BOOTREQUEST */
    pkt[pos++] = 1;              /* htype = Ethernet */
    pkt[pos++] = 6;              /* hlen = 6 */
    pkt[pos++] = 0;              /* hops = 0 */
    pkt[pos++] = xid>>8; pkt[pos++] = xid&0xFF;  /* xid (bytes 4-5) */
    pkt[pos++] = 0; pkt[pos++] = 0;              /* xid (bytes 6-7) */
    pkt[pos++] = 0; pkt[pos++] = 0;              /* secs = 0 */
    pkt[pos++] = 0; pkt[pos++] = 0;              /* flags = 0 */
    pkt[pos++] = yiaddr[0]; pkt[pos++] = yiaddr[1]; pkt[pos++] = yiaddr[2]; pkt[pos++] = yiaddr[3];  /* ciaddr */
    for (int i = 0; i < 4; i++) pkt[pos++] = yiaddr[i];  /* yiaddr */
    for (int i = 0; i < 12; i++) pkt[pos++] = 0;
    for (int i = 0; i < 16; i++) pkt[pos++] = 0;
    for (int i = 0; i < 64; i++) pkt[pos++] = 0;
    for (int i = 0; i < 8; i++) pkt[pos++] = 0;
    pkt[pos++] = 99; pkt[pos++] = 130;
    pkt[pos++] = 83; pkt[pos++] = 99;
    pkt[pos++] = 53; pkt[pos++] = 1; pkt[pos++] = DHCP_REQUEST;
    pkt[pos++] = 50; pkt[pos++] = 4;
    pkt[pos++] = yiaddr[0]; pkt[pos++] = yiaddr[1];
    pkt[pos++] = yiaddr[2]; pkt[pos++] = yiaddr[3];
    if (server_ip[0] || server_ip[1] || server_ip[2] || server_ip[3]) {
        pkt[pos++] = 54; pkt[pos++] = 4;
        pkt[pos++] = server_ip[0]; pkt[pos++] = server_ip[1];
        pkt[pos++] = server_ip[2]; pkt[pos++] = server_ip[3];
    }
    pkt[pos++] = 0xFF;

    send_udp_bc(DHCP_CLIENT_PORT, DHCP_SERVER_PORT, pkt, pos);

    rlen = recv_udp_port(DHCP_CLIENT_PORT, reply, 512);
    if (rlen < 240) return -1;

    /* Check for DHCP ACK */
    op_pos = 240;
    int ack = 0;
    while (op_pos < rlen) {
        if (op_pos >= rlen) break;
        u8 opt = reply[op_pos++];
        if (opt == 0) continue;
        if (opt == 0xFF) break;
        if (op_pos >= rlen) break;
        u8 olen = reply[op_pos++];
        if (op_pos + olen > rlen) break;
        if (opt == 53 && olen == 1 && reply[op_pos] == DHCP_ACK) ack = 1;
        op_pos += olen;
    }
    if (!ack) return -1;

    net_my_ip.addr[0] = yiaddr[0];
    net_my_ip.addr[1] = yiaddr[1];
    net_my_ip.addr[2] = yiaddr[2];
    net_my_ip.addr[3] = yiaddr[3];
    return 0;
}
