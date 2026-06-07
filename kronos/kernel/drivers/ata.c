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
static int ata_busy = 0;

int ata_init(void) {
    if (boot_drive >= 0x80) drive = 0xE0;
    else drive = 0xA0;
    outb(PORT_DRV, drive);
    inb(PORT_STAT);
    return 0;
}

void ata_select(int d) {
    if (d == 1) drive = 0xA0;
    else drive = 0xE0;
    outb(PORT_DRV, drive);
    for (volatile int i = 0; i < 10; i++);
}

static int wait_bsy(void) {
    u32 start = timer_ticks;
    while (inb(PORT_STAT) & 0x80) {
        if (timer_ticks - start > 10) return -1;
        task_yield();
    }
    return 0;
}

static int wait_drq(void) {
    u32 start = timer_ticks;
    while (1) {
        u8 s = inb(PORT_STAT);
        if (s & 0x08) return 0;
        if (s & 0x01) return -1;
        if (timer_ticks - start > 10) return -1;
        task_yield();
    }
}

static int ata_lock(void) {
    u32 start = timer_ticks;
    while (1) {
        __asm__ volatile("cli");
        if (!ata_busy) { ata_busy = 1; __asm__ volatile("sti"); return 0; }
        __asm__ volatile("sti");
        if (timer_ticks - start > 10) return -1;
        task_yield();
    }
}

static void ata_unlock(void) {
    __asm__ volatile("cli");
    ata_busy = 0;
    __asm__ volatile("sti");
}

static int ata_issue_read(unsigned lba) {
    if (wait_bsy()) return -1;
    outb(PORT_DRV, drive | ((lba >> 24) & 0x0F));
    outb(PORT_SC, 1);
    outb(PORT_LBA0, lba & 0xFF);
    outb(PORT_LBA1, (lba >> 8) & 0xFF);
    outb(PORT_LBA2, (lba >> 16) & 0xFF);
    outb(PORT_CMD, 0x20);
    return 0;
}

static int ata_issue_write(unsigned lba) {
    if (wait_bsy()) return -1;
    outb(PORT_DRV, drive | ((lba >> 24) & 0x0F));
    outb(PORT_SC, 1);
    outb(PORT_LBA0, lba & 0xFF);
    outb(PORT_LBA1, (lba >> 8) & 0xFF);
    outb(PORT_LBA2, (lba >> 16) & 0xFF);
    outb(PORT_CMD, 0x30);
    return 0;
}

int ata_read(unsigned lba, unsigned count, void *buf) {
    u8 *p = (u8 *)buf;
    if (ata_lock()) return -1;
    int ret = 0;
    for (unsigned s = 0; s < count; s++) {
        if (ata_issue_read(lba)) { ret = -1; break; }
        if (wait_drq()) { ret = -1; break; }
        for (int i = 0; i < 256; i++)
            ((u16 *)p)[i] = inw(PORT_DATA);
        p += 512;
        lba++;
    }
    ata_unlock();
    return ret;
}

int ata_write(unsigned lba, unsigned count, void *buf) {
    u8 *p = (u8 *)buf;
    if (ata_lock()) return -1;
    int ret = 0;
    for (unsigned s = 0; s < count; s++) {
        if (ata_issue_write(lba)) { ret = -1; break; }
        if (wait_drq()) { ret = -1; break; }
        for (int i = 0; i < 256; i++)
            outw(PORT_DATA, ((u16 *)p)[i]);
        outb(PORT_CMD, 0xE7);
        if (wait_bsy()) { ret = -1; break; }
        p += 512;
        lba++;
    }
    ata_unlock();
    return ret;
}
