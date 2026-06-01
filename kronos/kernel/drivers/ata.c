#include "ata.h"
#include "kernel.h"
#include "task.h"

extern u8 boot_drive;

#define PORT_DATA 0x1F0
#define PORT_ERR  0x1F1
#define PORT_SC   0x1F2
#define PORT_LBA0 0x1F3
#define PORT_LBA1 0x1F4
#define PORT_LBA2 0x1F5
#define PORT_DRV  0x1F6
#define PORT_CMD  0x1F7
#define PORT_STAT 0x1F7

static int drive = 0xE0;

int ata_init(void) {
    if (boot_drive >= 0x80) drive = 0xE0;
    else drive = 0xA0;
    outb(PORT_DRV, drive);
    inb(PORT_STAT);
    return 0;
}

static int wait_bsy(void) {
    u32 start = timer_ticks;
    for (int i = 0; i < 1000000; i++) {
        if (!(inb(PORT_STAT) & 0x80)) return 0;
        if (timer_ticks - start > 10) return -1;
    }
    return -1;
}

static int wait_drq(void) {
    u32 start = timer_ticks;
    for (int i = 0; i < 1000000; i++) {
        u8 s = inb(PORT_STAT);
        if (s & 0x08) return 0;
        if (s & 0x01) return -1;
        if (timer_ticks - start > 10) return -1;
    }
    return -1;
}

int ata_read(unsigned lba, unsigned count, void *buf) {
    u8 *p = (u8 *)buf;
    atomic_driver = 1;
    int ret = 0;
    for (unsigned s = 0; s < count; s++) {
        if (wait_bsy()) { ret = -1; break; }
        outb(PORT_DRV, drive | ((lba >> 24) & 0x0F));
        outb(PORT_SC, 1);
        outb(PORT_LBA0, lba & 0xFF);
        outb(PORT_LBA1, (lba >> 8) & 0xFF);
        outb(PORT_LBA2, (lba >> 16) & 0xFF);
        outb(PORT_CMD, 0x20);
        if (wait_drq()) { ret = -1; break; }
        for (int i = 0; i < 256; i++) {
            ((u16 *)p)[i] = inw(PORT_DATA);
        }
        p += 512;
        lba++;
    }
    atomic_driver = 0;
    return ret;
}

int ata_write(unsigned lba, unsigned count, void *buf) {
    u8 *p = (u8 *)buf;
    atomic_driver = 1;
    int ret = 0;
    for (unsigned s = 0; s < count; s++) {
        if (wait_bsy()) { ret = -1; break; }
        outb(PORT_DRV, drive | ((lba >> 24) & 0x0F));
        outb(PORT_SC, 1);
        outb(PORT_LBA0, lba & 0xFF);
        outb(PORT_LBA1, (lba >> 8) & 0xFF);
        outb(PORT_LBA2, (lba >> 16) & 0xFF);
        outb(PORT_CMD, 0x30);
        if (wait_drq()) { ret = -1; break; }
        for (int i = 0; i < 256; i++)
            outw(PORT_DATA, ((u16 *)p)[i]);
        outb(PORT_CMD, 0xE7);
        if (wait_bsy()) { ret = -1; break; }
        p += 512;
        lba++;
    }
    atomic_driver = 0;
    return ret;
}
