#include "heap.h"
#include "kernel.h"
#include "synch.h"

typedef struct blk {
    unsigned size;
    struct blk *next;
    unsigned magic;
    unsigned free;
} blk_t;

#define BSZ sizeof(blk_t)
#define HEAP_MAGIC 0xDEADBEEF
#define HEAP_ADDR 0x70000
#define HEAP_SIZE 0x20000

static blk_t *heap_base;
volatile u32 heap_brk = 0xA0000;

void heap_init(void) {
    heap_base = (blk_t *)HEAP_ADDR;
    heap_base->size = HEAP_SIZE - BSZ;
    heap_base->next = 0;
    heap_base->magic = HEAP_MAGIC;
    heap_base->free = 1;
}

void *kmalloc(unsigned size) {
    if (size == 0) return 0;
    if (!heap_base) return 0;
    if (size > 0xFFFFFFF8) return 0;
    mutex_lock(&mutex_heap);
    size = (size + 7) & ~7;
    if (size > HEAP_SIZE - BSZ) { mutex_unlock(&mutex_heap); return 0; }
    if (size + BSZ + 4 < size) { mutex_unlock(&mutex_heap); return 0; }
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
            mutex_unlock(&mutex_heap);
            return (char *)c + BSZ;
        }
        c = c->next;
    }
    mutex_unlock(&mutex_heap);
    return 0;
}

void kfree(void *ptr) {
    if (!ptr) return;
    mutex_lock(&mutex_heap);
    blk_t *b = (blk_t *)((char *)ptr - BSZ);
    if (b->magic != HEAP_MAGIC) { mutex_unlock(&mutex_heap); return; }
    if (b->free) { mutex_unlock(&mutex_heap); return; }
    b->free = 1;
    int merge_limit = 1000;
    blk_t *c = heap_base;
    while (c && c->next && --merge_limit > 0) {
        if (c->free && c->next->free) {
            c->size += BSZ + c->next->size;
            c->next = c->next->next;
        } else {
            c = c->next;
        }
    }
    mutex_unlock(&mutex_heap);
}

unsigned heap_get_brk(void) { return heap_brk; }

void heap_stats(int *used, int *free) {
    *used = 0; *free = 0;
    blk_t *c = heap_base;
    while (c) {
        if (c->free) *free += c->size; else *used += c->size;
        c = c->next;
    }
}
