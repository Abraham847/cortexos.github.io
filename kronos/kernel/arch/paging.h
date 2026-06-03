#ifndef PAGING_H
#define PAGING_H

#include "core.h"

#define PAGE_SIZE 4096
#define PAGE_SHIFT 12
#define PAGE_MASK 0xFFFFF000
#define PAGE_DIR_ENTRIES 1024
#define PAGE_TAB_ENTRIES 1024

#define PAGE_PRESENT 1
#define PAGE_WRITE   2
#define PAGE_USER    4

void paging_init(void);
u32 paging_alloc_frame(void);
void paging_free_frame(u32 addr);
int paging_map(u32 dir, u32 virt, u32 phys, u32 flags);
int paging_unmap(u32 dir, u32 virt);
u32 paging_resolve(u32 dir, u32 virt);
u32 paging_create_dir(void);
void paging_switch(u32 dir);
u32 paging_get_current(void);
void *paging_alloc_pages(u32 count);
void paging_free_pages(void *addr, u32 count);

#endif
