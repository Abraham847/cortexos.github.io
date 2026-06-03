#include "net.h"
#include "rtl8139.h"
#include "heap.h"

#define ARP_CACHE_SZ 8

static struct { ip4_t ip; u8 mac[6]; int valid; } arp_cache[ARP_CACHE_SZ];

ip4_t net_my_ip;
u8 net_my_mac[6];

static u16 net_id;

static int parse_octet(const char **p) {
    int v = 0;
    while (**p >= '0' && **p <= '9') {
        int d = **p - '0'; (*p)++;
        if (v > 214748364) { v = 2147483647; while (**p >= '0' && **p <= '9') (*p)++; break; }
        v = v * 10 + d;
    }
    return v;
}

void net_set_ip(const char *ip_str) {
    const char *p = ip_str;
    net_my_ip.addr[0] = parse_octet(&p);
    if (*p == '.') p++;
    net_my_ip.addr[1] = parse_octet(&p);
    if (*p == '.') p++;
    net_my_ip.addr[2] = parse_octet(&p);
    if (*p == '.') p++;
    net_my_ip.addr[3] = parse_octet(&p);
}

static int ip_cmp(ip4_t a, ip4_t b) {
    return a.addr[0] == b.addr[0] && a.addr[1] == b.addr[1]
        && a.addr[2] == b.addr[2] && a.addr[3] == b.addr[3];
}

static void arp_cache_add(ip4_t ip, const u8 *mac) {
    int oldest = 0;
    for (int i = 0; i < ARP_CACHE_SZ; i++) {
        if (!arp_cache[i].valid) { oldest = i; break; }
        if (ip_cmp(arp_cache[i].ip, ip)) { oldest = i; break; }
    }
    arp_cache[oldest].ip = ip;
    for (int i = 0; i < 6; i++) arp_cache[oldest].mac[i] = mac[i];
    arp_cache[oldest].valid = 1;
}

static int arp_cache_lookup(ip4_t ip, u8 *mac) {
    for (int i = 0; i < ARP_CACHE_SZ; i++) {
        if (arp_cache[i].valid && ip_cmp(arp_cache[i].ip, ip)) {
            for (int j = 0; j < 6; j++) mac[j] = arp_cache[i].mac[j];
            return 1;
        }
    }
    return 0;
}

int net_send_eth(const u8 *dst_mac, u16 type, const u8 *data, int len) {
    u8 pkt[ETH_HLEN + ETH_MTU];
    for (int i = 0; i < 6; i++) pkt[i] = dst_mac[i];
    for (int i = 0; i < 6; i++) pkt[6 + i] = net_my_mac[i];
    pkt[12] = type >> 8; pkt[13] = type & 0xFF;
    for (int i = 0; i < len; i++) pkt[ETH_HLEN + i] = data[i];
    return rtl8139_send(pkt, ETH_HLEN + len);
}

static void handle_arp(const u8 *data, int len) {
    if (len < sizeof(arp_pkt_t)) return;
    const arp_pkt_t *arp = (const arp_pkt_t*)data;
    if (arp->op == ARP_OP_REQUEST) {
        if (arp->tpa[0] == net_my_ip.addr[0] && arp->tpa[1] == net_my_ip.addr[1]
            && arp->tpa[2] == net_my_ip.addr[2] && arp->tpa[3] == net_my_ip.addr[3]) {
            arp_pkt_t rep;
            rep.htype = arp->htype; rep.ptype = arp->ptype;
            rep.hlen = 6; rep.plen = 4;
            rep.op = ARP_OP_REPLY;
            for (int i = 0; i < 6; i++) rep.sha[i] = net_my_mac[i];
            for (int i = 0; i < 4; i++) rep.spa[i] = net_my_ip.addr[i];
            for (int i = 0; i < 6; i++) rep.tha[i] = arp->sha[i];
            for (int i = 0; i < 4; i++) rep.tpa[i] = arp->spa[i];
            net_send_eth(arp->sha, ETH_TYPE_ARP, (const u8*)&rep, sizeof(rep));
        }
    } else if (arp->op == ARP_OP_REPLY) {
        arp_cache_add(*(ip4_t*)arp->spa, arp->sha);
    }
}

static u16 ip_chksum(const u16 *data, int nwords) {
    u32 sum = 0;
    for (int i = 0; i < nwords; i++) sum += data[i];
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return ~sum;
}

void net_poll(void) {
    u8 buf[ETH_MTU];
    int n;
    while ((n = rtl8139_recv(buf, ETH_MTU)) > 0) {
        if (n < ETH_HLEN) continue;
        u16 type = (buf[12] << 8) | buf[13];
        const u8 *payload = buf + ETH_HLEN;
        int plen = n - ETH_HLEN;
        if (type == ETH_TYPE_ARP) handle_arp(payload, plen);
        else if (type == ETH_TYPE_IP) {
            if (plen < (int)sizeof(ip_hdr_t)) continue;
            const ip_hdr_t *ip = (const ip_hdr_t*)payload;
            int ip_hdr_len = (ip->ver_ihl & 0x0F) * 4;
            if (ip_hdr_len < 20 || ip_hdr_len > plen) continue;
            if (ip->proto == IP_PROTO_ICMP && plen >= ip_hdr_len + 8) {
                const icmp_hdr_t *icmp = (const icmp_hdr_t*)(payload + ip_hdr_len);
                if (icmp->type == ICMP_TYPE_ECHO_REQ) {
                    u8 reply[plen];
                    for (int i = 0; i < plen; i++) reply[i] = payload[i];
                    ip_hdr_t *rip = (ip_hdr_t*)reply;
                    icmp_hdr_t *ricmp = (icmp_hdr_t*)(reply + ip_hdr_len);
                    ricmp->type = ICMP_TYPE_ECHO_REP;
                    ricmp->chksum = 0;
                    int icmp_len = plen - ip_hdr_len;
                    u32 sum = 0;
                    for (int i = 0; i < icmp_len / 2; i++) sum += ((u16*)ricmp)[i];
                    if (icmp_len & 1) sum += ((u8*)ricmp)[icmp_len - 1];
                    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
                    ricmp->chksum = ~sum;
                    for (int i = 0; i < 4; i++) {
                        u8 t = rip->src[i]; rip->src[i] = rip->dst[i]; rip->dst[i] = t;
                    }
                    rip->chksum = 0;
                    rip->chksum = ip_chksum((u16*)rip, ip_hdr_len / 2);
                    net_send_eth(buf + 6, ETH_TYPE_IP, reply, plen);
                }
            }
            if (ip->proto == IP_PROTO_UDP) {
                if (plen >= ip_hdr_len + 8) {
                    const udp_hdr_t *udp = (const udp_hdr_t*)(payload + ip_hdr_len);
                    (void)udp;
                }
            }
        }
    }
}

int net_arp_resolve(ip4_t ip, u8 *mac_out) {
    if (arp_cache_lookup(ip, mac_out)) return 1;
    static u8 bc[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    arp_pkt_t arp;
    arp.htype = ARP_HTYPE_ETH; arp.ptype = ARP_PTYPE_IP;
    arp.hlen = 6; arp.plen = 4;
    arp.op = ARP_OP_REQUEST;
    for (int i = 0; i < 6; i++) arp.sha[i] = net_my_mac[i];
    for (int i = 0; i < 4; i++) arp.spa[i] = net_my_ip.addr[i];
    for (int i = 0; i < 6; i++) arp.tha[i] = 0;
    for (int i = 0; i < 4; i++) arp.tpa[i] = ip.addr[i];
    net_send_eth(bc, ETH_TYPE_ARP, (const u8*)&arp, sizeof(arp));
    for (int t = 0; t < 50; t++) {
        net_poll();
        if (arp_cache_lookup(ip, mac_out)) return 1;
    }
    return 0;
}

int net_ping(ip4_t ip) {
    u8 mac[6];
    if (!net_arp_resolve(ip, mac)) return -1;
    u8 pkt[IP_HLEN + 8 + 4];
    ip_hdr_t *ip_hdr = (ip_hdr_t*)pkt;
    icmp_hdr_t *icmp = (icmp_hdr_t*)(pkt + IP_HLEN);
    ip_hdr->ver_ihl = IP_VER;
    ip_hdr->dscp = 0;
    ip_hdr->len = sizeof(pkt);
    ip_hdr->id = net_id++;
    ip_hdr->frag = 0;
    ip_hdr->ttl = 64;
    ip_hdr->proto = IP_PROTO_ICMP;
    for (int i = 0; i < 4; i++) ip_hdr->src[i] = net_my_ip.addr[i];
    for (int i = 0; i < 4; i++) ip_hdr->dst[i] = ip.addr[i];
    ip_hdr->chksum = 0;
    ip_hdr->chksum = ip_chksum((u16*)ip_hdr, IP_HLEN / 2);
    icmp->type = ICMP_TYPE_ECHO_REQ;
    icmp->code = 0;
    icmp->chksum = 0;
    u32 sum = 0;
    int icmp_len = 8 + 4;
    for (int i = 0; i < icmp_len / 2; i++) sum += ((u16*)icmp)[i];
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    icmp->chksum = ~sum;
    net_send_eth(mac, ETH_TYPE_IP, pkt, sizeof(pkt));
    return 0;
}

void net_init(void) {
    rtl8139_get_mac(net_my_mac);
    net_my_ip.addr[0] = 10;
    net_my_ip.addr[1] = 0;
    net_my_ip.addr[2] = 2;
    net_my_ip.addr[3] = 15;
    net_id = 1;
    for (int i = 0; i < ARP_CACHE_SZ; i++) arp_cache[i].valid = 0;
}
