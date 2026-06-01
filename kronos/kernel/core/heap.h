#ifndef HEAP_H
#define HEAP_H

void heap_init(void);
void *kmalloc(unsigned size);
void kfree(void *ptr);

#endif
