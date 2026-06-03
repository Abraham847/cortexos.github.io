#include "sb16.h"
#include "pci.h"

#define SB_BASE 0x220

#define DSP_RESET  (SB_BASE + 6)
#define DSP_READ   (SB_BASE + 0xA)
#define DSP_WRITE  (SB_BASE + 0xC)
#define DSP_BUSY   (SB_BASE + 0xE)
#define DSP_DATA16 (SB_BASE + 0xF)

#define DMA_8_ADDR  0x00
#define DMA_8_COUNT 0x01
#define DMA_8_MASK  0x0A
#define DMA_8_MODE  0x0B
#define DMA_8_CLRFF 0x0C
#define DMA_8_PAGE  0x83

static int sb_ready;

static void dsp_write(u8 v) {
    for (int t = 0; t < 10000; t++) {
        if (!(inb(DSP_BUSY) & 0x80)) { outb(DSP_WRITE, v); return; }
    }
}

static u8 dsp_read(void) {
    for (int t = 0; t < 10000; t++) {
        if (inb(DSP_READ) & 0x80) return inb(DSP_READ + 1);
    }
    return 0;
}

void sb16_init(void) {
    outb(DSP_RESET, 1);
    for (volatile int i = 0; i < 100; i++);
    outb(DSP_RESET, 0);
    if (dsp_read() != 0xAA) return;
    sb_ready = 1;
    dsp_write(0xD1);
}

int sb16_play(const u8 *data, int len, int freq) {
    if (!sb_ready) return -1;
    if (len > 65535) len = 65535;

    u32 buf_phys = (u32)data;
    if (buf_phys >= 0x1000000) return -1;
    u8 page = (buf_phys >> 16) & 0xFF;
    u16 addr = buf_phys & 0xFFFF;
    u16 count = len - 1;

    outb(DMA_8_MASK, 5);
    outb(DMA_8_CLRFF, 0);
    outb(DMA_8_ADDR, addr & 0xFF);
    outb(DMA_8_ADDR, (addr >> 8) & 0xFF);
    outb(DMA_8_PAGE, page);
    outb(DMA_8_COUNT, count & 0xFF);
    outb(DMA_8_COUNT, (count >> 8) & 0xFF);
    outb(DMA_8_MODE, 0x48);
    outb(DMA_8_MASK, 1);

    u16 time_const = 256 - (1000000 / freq);
    dsp_write(0x40);
    dsp_write(time_const & 0xFF);

    dsp_write(0x14);
    dsp_write(len & 0xFF);
    dsp_write((len >> 8) & 0xFF);

    return len;
}

int sb16_set_volume(int vol) {
    if (!sb_ready) return -1;
    if (vol < 0) vol = 0;
    if (vol > 255) vol = 255;
    outb(SB_BASE + 4, 0x22);
    outb(SB_BASE + 5, vol);
    return 0;
}
