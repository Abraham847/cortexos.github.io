#ifndef HEAP_H
#define HEAP_H

void heap_init(void);
void *kmalloc(unsigned size);
void kfree(void *ptr);
void heap_stats(int *used, int *free);
unsigned heap_get_brk(void);

#endif
