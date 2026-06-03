#ifndef NET_H
#define NET_H

#include "core.h"

#define ETH_ALEN 6
#define ETH_HLEN 14
#define ETH_MTU 1500
#define ETH_TYPE_IP 0x0800
#define ETH_TYPE_ARP 0x0806

#define IP_HLEN 20
#define IP_VER 0x45

#define IP_PROTO_ICMP 1
#define IP_PROTO_TCP 6
#define IP_PROTO_UDP 17

#define ARP_HTYPE_ETH 1
#define ARP_PTYPE_IP 0x0800
#define ARP_OP_REQUEST 1
#define ARP_OP_REPLY 2

#define ICMP_TYPE_ECHO_REQ 8
#define ICMP_TYPE_ECHO_REP 0

#pragma pack(push, 1)
typedef struct { u8 dst[6], src[6]; u16 type; } eth_hdr_t;
typedef struct { u16 htype, ptype; u8 hlen, plen; u16 op; u8 sha[6], spa[4], tha[6], tpa[4]; } arp_pkt_t;
typedef struct { u8 ver_ihl, dscp; u16 len, id, frag; u8 ttl, proto; u16 chksum; u8 src[4], dst[4]; } ip_hdr_t;
typedef struct { u8 type, code; u16 chksum; } icmp_hdr_t;
typedef struct { u16 sport, dport, len, chksum; } udp_hdr_t;
#pragma pack(pop)

typedef struct { u8 addr[4]; } ip4_t;

extern ip4_t net_my_ip;
extern u8 net_my_mac[6];

void net_init(void);
void net_poll(void);

int net_send_eth(const u8 *dst_mac, u16 type, const u8 *data, int len);
int net_arp_resolve(ip4_t ip, u8 *mac_out);
void net_set_ip(const char *ip_str);

int net_ping(ip4_t ip);

#endif
