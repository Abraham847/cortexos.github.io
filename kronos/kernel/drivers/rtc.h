#ifndef RTC_H
#define RTC_H

#include "core.h"

typedef struct {
    int year, mon, day, hour, min, sec;
} rtc_time_t;

void rtc_init(void);
void rtc_read(rtc_time_t *t);
int rtc_get_uptime_str(char *buf, int max);

#endif
