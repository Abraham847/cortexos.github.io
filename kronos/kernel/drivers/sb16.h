#ifndef SB16_H
#define SB16_H

#include "core.h"

void sb16_init(void);
int sb16_play(const u8 *data, int len, int freq);
int sb16_set_volume(int vol);

#endif
