#include "rtc.h"

#define CMOS_ADDR 0x70
#define CMOS_DATA 0x71

static int bcd;

static u8 cmos_read(u8 reg) {
    outb(CMOS_ADDR, reg);
    return inb(CMOS_DATA);
}

static u8 rtc_get(u8 reg) {
    u8 v = cmos_read(reg);
    if (bcd) return (v >> 4) * 10 + (v & 0x0F);
    return v;
}

void rtc_init(void) {
    u8 st = cmos_read(0x0B);
    bcd = !(st & 0x04);
}

void rtc_read(rtc_time_t *t) {
    u8 sec, min, hour, day, mon, yr, cen;
    int tries = 0;
    do {
        sec = rtc_get(0x00);
        min = rtc_get(0x02);
        hour = rtc_get(0x04);
        day = rtc_get(0x07);
        mon = rtc_get(0x08);
        yr = rtc_get(0x09);
        cen = rtc_get(0x32);
    } while (sec != rtc_get(0x00) && tries++ < 3);

    t->sec = sec;
    t->min = min;
    t->hour = hour;
    t->day = day;
    t->mon = mon;
    t->year = cen * 100 + yr;
}

int rtc_get_uptime_str(char *buf, int max) {
    rtc_time_t t;
    rtc_read(&t);
    char tmp[16];
    int pos = 0;
    itoa(t.year, tmp);
    int i; for (i = 0; tmp[i] && pos < max - 1; i++) buf[pos++] = tmp[i];
    if (pos < max - 1) buf[pos++] = '-';
    itoa(t.mon, tmp); if (t.mon < 10 && pos < max - 1) buf[pos++] = '0';
    for (i = 0; tmp[i] && pos < max - 1; i++) buf[pos++] = tmp[i];
    if (pos < max - 1) buf[pos++] = '-';
    itoa(t.day, tmp); if (t.day < 10 && pos < max - 1) buf[pos++] = '0';
    for (i = 0; tmp[i] && pos < max - 1; i++) buf[pos++] = tmp[i];
    buf[pos] = 0;
    return pos;
}
