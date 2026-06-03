#ifndef RTL8139_H
#define RTL8139_H

#include "core.h"

#define RTL8139_VENDOR 0x10EC
#define RTL8139_DEVICE 0x8139

void rtl8139_init(void);
int rtl8139_send(const u8 *data, int len);
int rtl8139_recv(u8 *buf, int max);
int rtl8139_is_link_up(void);
void rtl8139_get_mac(u8 *mac);

#endif
