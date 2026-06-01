#include "heap.h"
#include "kernel.h"
#include "task.h"

typedef struct blk {
    unsigned size;
    struct blk *next;
    unsigned magic;
    unsigned free;
} blk_t;

#define BSZ sizeof(blk_t)
#define HEAP_MAGIC 0xDEADBEEF

static blk_t *heap_base;

void heap_init(void) {
    extern u8 __bss_end[];
    unsigned addr = (unsigned)__bss_end;
    addr = (addr + 7) & ~7;
    heap_base = (blk_t *)addr;
    heap_base->size = 0x20000 - BSZ;
    heap_base->next = 0;
    heap_base->magic = HEAP_MAGIC;
    heap_base->free = 1;
}

void *kmalloc(unsigned size) {
    if (size == 0) return 0;
    if (!heap_base) return 0;
    atomic_driver = 1;
    size = (size + 7) & ~7;
    blk_t *c = heap_base;
    while (c) {
        if (c->free && c->size >= size) {
            if (c->size > size + BSZ + 4) {
                blk_t *n = (blk_t *)((char *)c + BSZ + size);
                n->size = c->size - size - BSZ;
                n->free = 1;
                n->magic = HEAP_MAGIC;
                n->next = c->next;
                c->size = size;
                c->next = n;
            }
            c->free = 0;
            c->magic = HEAP_MAGIC;
            atomic_driver = 0;
            return (char *)c + BSZ;
        }
        c = c->next;
    }
    atomic_driver = 0;
    return 0;
}

void kfree(void *ptr) {
    if (!ptr) return;
    atomic_driver = 1;
    blk_t *b = (blk_t *)((char *)ptr - BSZ);
    if (b->magic != HEAP_MAGIC) { atomic_driver = 0; return; }
    if (b->free) { atomic_driver = 0; return; }
    b->free = 1;
    blk_t *c = heap_base;
    while (c && c->next) {
        if (c->free && c->next->free) {
            c->size += BSZ + c->next->size;
            c->next = c->next->next;
        } else {
            c = c->next;
        }
    }
    atomic_driver = 0;
}
