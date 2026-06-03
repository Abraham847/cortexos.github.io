#ifndef KB_H
#define KB_H

#include "core.h"

void kb_init(void);
char kb_getchar(void);
void kb_gets(char *buf, int max);
int kb_keypressed(void);
void kb_process(void);

#endif
