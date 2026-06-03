#include "uhci.h"
#include "pci.h"

static u16 uhci_io;
static int uhci_ready;

/* UHCI ports */
#define USBCMD   0
#define USBSTS   2
#define USBINTR  4
#define FRNUM    6
#define FLBASE   8
#define SOFMOD   12
#define PORTSC1  16
#define PORTSC2  18

/* USB control transfer phases */
#define SETUP_PID 0x2D
#define IN_PID    0x69
#define OUT_PID   0xE1
#define DATA0_PID 0xC3
#define DATA1_PID 0x4B

/* TD token fields */
#define PID_IN    0x69
#define PID_SETUP 0x2D
#define PID_OUT   0xE1
#define TOKEN(pid, endp, dev, data) ((pid) | ((endp)<<7) | ((dev)<<15) | ((data)<<19))
#define TD_CTRL_ACTIVE   (1<<23)
#define TD_CTRL_IOC      (1<<24)

/* descriptor types */
#define DESC_DEVICE   1
#define DESC_CONFIG   2
#define DESC_STRING   3
#define DESC_HID      0x21
#define DESC_REPORT   0x22

#define USB_REQ_GET_DESC       6
#define USB_REQ_SET_ADDR       5
#define USB_REQ_SET_CONF       9
#define USB_REQ_SET_IDLE      0x0A
#define USB_REQ_SET_PROTOCOL  0x0B

#define USB_DEV_CLASS_HID     3

/* aligned buffer for descriptors */
static u8 desc_buf[256] __attribute__((aligned(4096)));

/* Frame list: 1024 entries = 4096 bytes */
static volatile u32 *flist; /* allocated below */

/* TD / QH pool */
#define TD_POOL 32
#define QH_POOL 8
struct td { u32 link; u32 ctrl; u32 token; u32 buf; u32 rsv[4]; } __attribute__((packed));
struct qh { u32 head_link; u32 elem_link; u32 rsv[4]; } __attribute__((packed));
static struct td td_pool[TD_POOL] __attribute__((aligned(32)));
static struct qh qh_pool[QH_POOL] __attribute__((aligned(32)));
static int td_used[TD_POOL], qh_used[QH_POOL];

/* device state */
static int usb_dev_addr;
static int usb_dev_ep;
static int usb_kb_intf;
static u8 kb_report[8];
static int kb_report_ready;

static u32 td_pa(int i) { return (u32)&td_pool[i]; }
static u32 qh_pa(int i) { return (u32)&qh_pool[i]; }

static int td_alloc(void) {
    for (int i = 0; i < TD_POOL; i++) if (!td_used[i]) { td_used[i]=1; return i; }
    return -1;
}
static void td_free(int i) { if(i>=0)td_used[i]=0;memset(&td_pool[i],0,32); }

static int qh_alloc(void) {
    for (int i = 0; i < QH_POOL; i++) if (!qh_used[i]) { qh_used[i]=1; return i; }
    return -1;
}
static void qh_free(int i) { if(i>=0)qh_used[i]=0;memset(&qh_pool[i],0,24); }

static u16 uhci_r16(u8 off) { return inw(uhci_io + off); }
static void uhci_w16(u8 off, u16 v) { outw(uhci_io + off, v); }
static void uhci_w8(u8 off, u8 v) { outb(uhci_io + off, v); }

static void td_setup(struct td *t, u32 link, u8 pid, u8 endp, u8 dev, u8 data, u32 buf, int maxlen, u32 ctrl) {
    t->link = link;
    t->ctrl = ctrl | TD_CTRL_ACTIVE;
    t->token = TOKEN(pid, endp, dev, data) | (maxlen << 21);
    t->buf = buf;
}

static void prepare_ctrl(int dev, u8 bmReqType, u8 bRequest, u16 wValue, u16 wIndex, u16 wLength, u8 *data, u8 data1) {
    struct usb_setup { u8 bm; u8 req; u16 val; u16 idx; u16 len; } __attribute__((packed));
    struct usb_setup *s = (struct usb_setup *)desc_buf;
    s->bm = bmReqType;
    s->req = bRequest;
    s->val = wValue;
    s->idx = wIndex;
    s->len = wLength;
    /* Use the first TD pool entries */
    int t0 = td_alloc(); if(t0<0)return;
    int t1 = td_alloc(); if(t1<0){td_free(t0);return;}
    int t2 = -1; if(data) { t2=td_alloc(); if(t2<0){td_free(t0);td_free(t1);return;} }
    /* SETUP TD */
    td_setup(&td_pool[t0], (t1>=0?td_pa(t1):0x02)|2, PID_SETUP, 0, dev, 0, (u32)s, 8, 0);
    /* DATA0 or DATA1 TD */
    if(t2>=0){
        td_setup(&td_pool[t1], td_pa(t2), data1?DATA1_PID:DATA0_PID, 0, dev, 0, (u32)data, wLength, 0);
        td_setup(&td_pool[t2], 0x02, PID_IN, 0, dev, 1, (u32)desc_buf, 0, 0);
    } else {
        td_setup(&td_pool[t1], 0x02, data1?DATA1_PID:DATA0_PID, 0, dev, 1, (u32)desc_buf, 0, 0);
    }
    /* Set async QH to point to first TD */
    qh_pool[0].head_link = td_pa(t0);
    qh_pool[0].elem_link = td_pa(t0);
}

void uhci_init(void) {
    pci_dev_t dev;
    if (!pci_scan(0x8086, 0x7020, &dev) && !pci_scan(0x8086, 0x2412, &dev)) {
        return;
    }
    uhci_io = dev.bar0 & 0xFFF0;
    if (!uhci_io) return;

    /* Allocate frame list (4096 bytes aligned) */
    flist = (volatile u32 *)desc_buf; /* reuse desc_buf since it's 4K aligned */
    memset((void*)flist, 0, 4096);
    /* Mark all frame list entries as terminate */
    for (int i = 0; i < 1024; i++) flist[i] = 0x0001;

    /* Reset UHCI */
    uhci_w16(USBCMD, 0x0004);
    for (volatile int i = 0; i < 1000; i++);
    uhci_w16(USBCMD, 0x0000);
    for (volatile int i = 0; i < 1000; i++);

    /* Set frame length */
    uhci_w16(SOFMOD, 64);

    /* Set frame list base */
    uhci_w32:;
    u32 fl_phys = (u32)flist;
    outl(uhci_io + FLBASE, fl_phys);

    /* Set max packet for port 1 - assume 8 bytes for low speed */
    /* Reset port */
    u16 ps = uhci_r16(PORTSC1);
    uhci_w16(PORTSC1, ps | 0x0200);
    for (volatile int i = 0; i < 10000; i++);
    uhci_w16(PORTSC1, ps | 0x0200);
    for (volatile int i = 0; i < 10000; i++);
    uhci_w16(PORTSC1, ps | 0x0200);
    for (volatile int i = 0; i < 10000; i++);
    /* Clear reset */
    ps = uhci_r16(PORTSC1);
    uhci_w16(PORTSC1, ps & ~0x0200);
    for (volatile int i = 0; i < 10000; i++);

    /* Check if device connected */
    if (!(uhci_r16(PORTSC1) & 1)) return;

    /* Start UHCI */
    uhci_w16(USBCMD, 0x0001);

    /* Setup async QH */
    qh_pool[0].head_link = 0x0002;
    qh_pool[0].elem_link = 0x0002;

    /* === Enumeration === */
    /* Get device descriptor (first 8 bytes to get max packet) */
    /* We use qh_pool[0] for async schedule */
    prepare_ctrl(0, 0x80, USB_REQ_GET_DESC, DESC_DEVICE<<8, 0, 8, 0, 0);
    for (volatile int i = 0; i < 5000; i++);

    /* Set address = 1 */
    usb_dev_addr = 1;
    prepare_ctrl(0, 0x00, USB_REQ_SET_ADDR, usb_dev_addr, 0, 0, 0, 1);
    for (volatile int i = 0; i < 5000; i++);

    /* Get full device descriptor */
    prepare_ctrl(usb_dev_addr, 0x80, USB_REQ_GET_DESC, DESC_DEVICE<<8, 0, 18, (u8*)desc_buf, 0);
    for (volatile int i = 0; i < 5000; i++);

    /* Get config descriptor */
    prepare_ctrl(usb_dev_addr, 0x80, USB_REQ_GET_DESC, DESC_CONFIG<<8, 0, 9, (u8*)desc_buf, 0);
    for (volatile int i = 0; i < 5000; i++);

    /* Set configuration */
    prepare_ctrl(usb_dev_addr, 0x00, USB_REQ_SET_CONF, 1, 0, 0, 0, 1);
    for (volatile int i = 0; i < 5000; i++);

    /* Set idle (only for HID) */
    /* Set protocol to boot */
    prepare_ctrl(usb_dev_addr, 0x21, USB_REQ_SET_PROTOCOL, 0, 0, 0, 0, 1);
    for (volatile int i = 0; i < 5000; i++);

    usb_dev_ep = 0x81;
    uhci_ready = 1;
}

int uhci_poll_kb(void) {
    if (!uhci_ready) return 0;
    /* Queue an interrupt IN transfer */
    int td = td_alloc();
    if (td < 0) return 0;
    td_setup(&td_pool[td], 0x02, PID_IN, usb_dev_ep & 0x7F, usb_dev_addr, 1, (u32)kb_report, 8, 0);
    qh_pool[0].elem_link = td_pa(td);
    for (volatile int i = 0; i < 5000; i++);
    kb_report_ready = 1;
    td_free(td);
    return 1;
}

int uhci_get_key(void) {
    if (!uhci_ready || !kb_report_ready) return 0;
    u8 k = kb_report[2]; /* byte 2 = keycode */
    if (k == 0) return 0;
    /* USB HID keycode to ASCII (basic alphanumeric) */
    if (k >= 4 && k <= 29) return 'a' + k - 4;
    if (k >= 30 && k <= 39) return '1' + k - 30;
    if (k == 40) return '\n';
    if (k == 41) return 27;
    if (k == 42) return 8;
    if (k == 43) return '\t';
    if (k == 44) return ' ';
    if (k == 45) return '-';
    if (k == 46) return '=';
    if (k == 47) return '[';
    if (k == 48) return ']';
    if (k == 49) return '\\';
    if (k == 51) return ';';
    if (k == 52) return '\'';
    if (k == 53) return '`';
    if (k == 54) return ',';
    if (k == 55) return '.';
    if (k == 56) return '/';
    return 0;
}
