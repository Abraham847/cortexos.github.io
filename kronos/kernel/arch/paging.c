#include "paging.h"
#include "synch.h"

#define PHYSICAL_MEM_SIZE (64 * 1024 * 1024)
#define PAGE_FRAMES (PHYSICAL_MEM_SIZE / PAGE_SIZE)
#define BITMAP_SIZE (PAGE_FRAMES / 8)

static u32 page_bitmap[BITMAP_SIZE / 4];
static u32 kernel_page_dir[PAGE_DIR_ENTRIES] __attribute__((aligned(PAGE_SIZE)));
static u32 kernel_page_tab[PAGE_TAB_ENTRIES] __attribute__((aligned(PAGE_SIZE)));
static u32 temp_page_tab[PAGE_TAB_ENTRIES] __attribute__((aligned(PAGE_SIZE)));
static u32 current_page_dir;
static mutex_t page_mutex = MUTEX_INIT;

#define PAGE_BITMAP_ADDR ((u32)page_bitmap)
#define KERNEL_DIR_ADDR  ((u32)kernel_page_dir)
#define KERNEL_TAB_ADDR  ((u32)kernel_page_tab)
#define TEMP_TAB_ADDR    ((u32)temp_page_tab)

static void page_bitmap_set(int idx) {
    page_bitmap[idx / 32] |= (1 << (idx % 32));
}

static void page_bitmap_clear(int idx) {
    page_bitmap[idx / 32] &= ~(1 << (idx % 32));
}

static int page_bitmap_test(int idx) {
    return (page_bitmap[idx / 32] >> (idx % 32)) & 1;
}

static int page_bitmap_find(void) {
    int low_max = 4 * 1024 * 1024 / PAGE_SIZE;
    int limit = low_max < PAGE_FRAMES ? low_max : PAGE_FRAMES;
    for (int i = 0; i < limit; i++) {
        if (!page_bitmap_test(i)) return i;
    }
    return -1;
}

void paging_init(void) {
    for (int i = 0; i < BITMAP_SIZE / 4; i++) page_bitmap[i] = 0;

    for (int i = 0; i < PAGE_DIR_ENTRIES; i++) {
        kernel_page_dir[i] = 0;
        kernel_page_tab[i] = 0;
        temp_page_tab[i] = 0;
    }

    for (int i = 0; i < PAGE_TAB_ENTRIES; i++) {
        kernel_page_tab[i] = (i * PAGE_SIZE) | PAGE_PRESENT | PAGE_WRITE;
        page_bitmap_set(i);
    }

    kernel_page_dir[0] = KERNEL_TAB_ADDR | PAGE_PRESENT | PAGE_WRITE;

    kernel_page_dir[KERNEL_DIR_ADDR >> 22] = KERNEL_TAB_ADDR | PAGE_PRESENT | PAGE_WRITE;

    __asm__ volatile("mov %0, %%cr3" : : "r"(KERNEL_DIR_ADDR));

    u32 cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));

    current_page_dir = KERNEL_DIR_ADDR;
}

u32 paging_alloc_frame(void) {
    mutex_lock(&page_mutex);
    int idx = page_bitmap_find();
    if (idx < 0) { mutex_unlock(&page_mutex); return 0; }
    page_bitmap_set(idx);
    mutex_unlock(&page_mutex);
    u32 addr = idx * PAGE_SIZE;
    memset((void*)addr, 0, PAGE_SIZE);
    return addr;
}

void paging_free_frame(u32 addr) {
    int idx = addr / PAGE_SIZE;
    if (idx < 0 || idx >= PAGE_FRAMES) return;
    page_bitmap_clear(idx);
}

int paging_map(u32 dir, u32 virt, u32 phys, u32 flags) {
    u32 dir_idx = virt >> 22;
    u32 tab_idx = (virt >> 12) & 0x3FF;

    u32 *page_dir = (u32*)dir;
    u32 *page_tab;

    if (!(page_dir[dir_idx] & PAGE_PRESENT)) {
        u32 new_tab = paging_alloc_frame();
        if (!new_tab) return -1;
        page_dir[dir_idx] = new_tab | PAGE_PRESENT | PAGE_WRITE | (flags & PAGE_USER);
        __asm__ volatile("invlpg %0" : : "m"(*(volatile u32*)virt));
    }

    page_tab = (u32*)(page_dir[dir_idx] & PAGE_MASK);

    page_tab[tab_idx] = (phys & PAGE_MASK) | PAGE_PRESENT | (flags & (PAGE_WRITE | PAGE_USER));
    __asm__ volatile("invlpg %0" : : "m"(*(volatile u32*)virt));
    return 0;
}

int paging_unmap(u32 dir, u32 virt) {
    u32 dir_idx = virt >> 22;
    u32 tab_idx = (virt >> 12) & 0x3FF;
    u32 *page_dir = (u32*)dir;

    if (!(page_dir[dir_idx] & PAGE_PRESENT)) return -1;

    u32 *page_tab = (u32*)(page_dir[dir_idx] & PAGE_MASK);
    page_tab[tab_idx] = 0;
    __asm__ volatile("invlpg %0" : : "m"(*(volatile u32*)virt));
    return 0;
}

u32 paging_resolve(u32 dir, u32 virt) {
    u32 dir_idx = virt >> 22;
    u32 tab_idx = (virt >> 12) & 0x3FF;
    u32 *page_dir = (u32*)dir;

    if (!(page_dir[dir_idx] & PAGE_PRESENT)) return 0;

    u32 *page_tab = (u32*)(page_dir[dir_idx] & PAGE_MASK);
    if (!(page_tab[tab_idx] & PAGE_PRESENT)) return 0;

    return (page_tab[tab_idx] & PAGE_MASK) | (virt & 0xFFF);
}

u32 paging_create_dir(void) {
    u32 new_dir = paging_alloc_frame();
    if (!new_dir) return 0;
    u32 *dir = (u32*)new_dir;

    for (int i = 0; i < PAGE_DIR_ENTRIES; i++) dir[i] = 0;

    u32 *kernel_dir = (u32*)KERNEL_DIR_ADDR;
    dir[KERNEL_DIR_ADDR >> 22] = kernel_dir[KERNEL_DIR_ADDR >> 22];

    return new_dir;
}

void paging_switch(u32 dir) {
    __asm__ volatile("mov %0, %%cr3" : : "r"(dir));
    current_page_dir = dir;
}

u32 paging_get_current(void) {
    return current_page_dir;
}

void *paging_alloc_pages(u32 count) {
    if (count == 0) return 0;
    mutex_lock(&page_mutex);
    int start = -1;
    int run = 0;
    for (int i = 0; i < PAGE_FRAMES; i++) {
        if (!page_bitmap_test(i)) {
            if (start < 0) start = i;
            run++;
            if (run == count) break;
        } else {
            start = -1; run = 0;
        }
    }
    if (run < count) { mutex_unlock(&page_mutex); return 0; }

    u32 addr = start * PAGE_SIZE;
    for (int i = 0; i < count; i++) {
        page_bitmap_set(start + i);
        memset((void*)(addr + i * PAGE_SIZE), 0, PAGE_SIZE);
    }
    mutex_unlock(&page_mutex);
    return (void*)addr;
}

void paging_free_pages(void *addr, u32 count) {
    u32 base = (u32)addr;
    for (u32 i = 0; i < count; i++) {
        paging_free_frame(base + i * PAGE_SIZE);
    }
}
