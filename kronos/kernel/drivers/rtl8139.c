#include "rtl8139.h"
#include "pci.h"
#include "core.h"

#define RX_BUF_LEN 8192
#define TX_BUF_LEN 1536

static u16 io_port;
static u8 rx_ring[RX_BUF_LEN + 16] __attribute__((aligned(4)));
static u16 rx_cur;
static u8 mac[6];
static int ready;

static u8 rr(u8 r) { return inb(io_port + r); }
static u16 rw(u8 r) { return inw(io_port + r); }
static u32 rl(u8 r) { return inl(io_port + r); }
static void w8(u8 r, u8 v) { outb(io_port + r, v); }
static void w16(u8 r, u16 v) { outw(io_port + r, v); }
static void w32(u8 r, u32 v) { outl(io_port + r, v); }

void rtl8139_init(void) {
    pci_dev_t dev;
    if (!pci_scan(RTL8139_VENDOR, RTL8139_DEVICE, &dev)) return;

    io_port = dev.bar0 & ~3;
    pci_write_cfg(dev.bus, dev.slot, dev.func, 0x04, 0x05);

    w8(0x52, 0);

    for (int i = 0; i < 6; i++) mac[i] = rr(i);

    w32(0x30, (u32)rx_ring);
    rx_cur = 0;
    w16(0x38, rx_cur - 16);

    w16(0x44, 0xFF00);
    w8(0x37, 0x0C);
    w8(0x37, 0x0E);
    w8(0x44, 1);

    ready = 1;
}

int rtl8139_send(const u8 *data, int len) {
    if (!ready) return -1;
    if (len > TX_BUF_LEN) len = TX_BUF_LEN;

    static u8 tx_buf[TX_BUF_LEN] __attribute__((aligned(4)));
    for (int i = 0; i < len; i++) tx_buf[i] = data[i];

    w32(0x20, (u32)tx_buf);
    w32(0x24, 0);
    w16(0x3C, 0x003E);

    for (int t = 0; t < 100; t++) {
        u32 s = rl(0x3C);
        if (!(s & 0x003E)) break;
    }

    w16(0x3C, len | 0x0100);
    return len;
}

int rtl8139_recv(u8 *buf, int max) {
    if (!ready) return -1;

    u16 rx_size = 8192 + 16;
    if (rx_cur >= rx_size) rx_cur = 0;

    volatile u8 *pkt = &rx_ring[rx_cur];
    u16 hdr = *(volatile u16*)pkt;
    u16 pkt_len = *(volatile u16*)(pkt + 2);

    if (hdr & 0x8000) {
        w16(0x38, rx_cur - 16);
        return 0;
    }

    if (pkt_len == 0xFFF0 || pkt_len < 4) return 0;

    pkt_len -= 4;
    int n = pkt_len;
    if (n > max) n = max;
    for (int i = 0; i < n; i++) buf[i] = pkt[i + 4];

    rx_cur = (rx_cur + pkt_len + 4 + 3) & ~3;
    if (rx_cur >= rx_size) rx_cur -= rx_size;

    w16(0x38, rx_cur - 16);
    return n;
}

int rtl8139_is_link_up(void) {
    if (!ready) return 0;
    return (rr(0x5A) & 0x40) ? 0 : 1;
}

void rtl8139_get_mac(u8 *mac_out) {
    for (int i = 0; i < 6; i++) mac_out[i] = mac[i];
}
