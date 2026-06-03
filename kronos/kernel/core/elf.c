#include "elf.h"
#include "vfs.h"
#include "heap.h"
#include "paging.h"

#define USER_STACK_ADDR 0xBFFFF000
#define USER_STACK_PAGES 4

static int copy_from_file(const char *path, u8 **out, u32 *out_size) {
    file_t f;
    if (vfs_open(path, O_RDONLY, 0, &f) < 0) return -1;
    stat_t s;
    if (vfs_stat(&f, &s) < 0) { vfs_close(&f); return -1; }
    u32 size = s.size;
    u8 *buf = (u8*)kmalloc(size + 1);
    if (!buf) { vfs_close(&f); return -1; }
    memset(buf, 0, size + 1);
    int r = vfs_read(&f, buf, size);
    vfs_close(&f);
    if (r < 0) { kfree(buf); return -1; }
    *out = buf;
    *out_size = size;
    return 0;
}

static int map_user_pages(u32 vaddr, u32 size) {
    u32 start_page = vaddr & 0xFFFFF000;
    u32 end = (vaddr + size + 0xFFF) & 0xFFFFF000;
    u32 page_dir = paging_get_current();
    for (u32 page = start_page; page < end; page += 0x1000) {
        u32 phys = paging_alloc_frame();
        if (!phys) return -1;
        paging_map(page_dir, page, phys, PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
    }
    return 0;
}

static void setup_user_stack(u32 *stack_top_out) {
    u32 stack_top = USER_STACK_ADDR + 0x1000;
    u32 page_dir = paging_get_current();
    for (int i = 0; i < USER_STACK_PAGES; i++) {
        u32 page = USER_STACK_ADDR + i * 0x1000;
        u32 phys = paging_alloc_frame();
        if (phys) {
            paging_map(page_dir, page, phys, PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
        }
    }
    *stack_top_out = stack_top;
}

int elf_load(const char *path, u32 *entry, u32 *stack_top) {
    u8 *elf_data;
    u32 elf_size;

    if (copy_from_file(path, &elf_data, &elf_size) < 0) return -1;

    elf32_hdr_t *hdr = (elf32_hdr_t*)elf_data;
    if (hdr->magic != ELF_MAGIC || hdr->cls != 1 || hdr->data != 1 || hdr->machine != 3) {
        kfree(elf_data);
        return -1;
    }

    if (hdr->phentsize != sizeof(elf32_phdr_t) || hdr->phnum == 0) {
        kfree(elf_data);
        return -1;
    }

    u32 phoff = hdr->phoff;
    int phnum = hdr->phnum;

    if (phoff + phnum * sizeof(elf32_phdr_t) > elf_size) {
        kfree(elf_data);
        return -1;
    }

    for (int i = 0; i < phnum; i++) {
        elf32_phdr_t *ph = (elf32_phdr_t*)(elf_data + phoff + i * sizeof(elf32_phdr_t));
        if (ph->type == PT_LOAD) {
            if (ph->memsz == 0) continue;
            u32 vaddr = ph->vaddr;
            u32 memsz = ph->memsz;
            u32 filesz = ph->filesz;
            u32 offset = ph->offset;

            if (vaddr + memsz > 0x40000000) {
                kfree(elf_data);
                return -1;
            }

            if (map_user_pages(vaddr, memsz) < 0) {
                kfree(elf_data);
                return -1;
            }

            if (filesz > 0) {
                if (offset + filesz > elf_size) {
                    kfree(elf_data);
                    return -1;
                }
                u32 seg_end = vaddr + filesz;
                for (u32 p = vaddr; p < seg_end; p++) {
                    u32 src_off = offset + (p - vaddr);
                    if (src_off < elf_size) {
                        *(u8*)p = elf_data[src_off];
                    }
                }
            }

            if (memsz > filesz) {
                memset((void*)(vaddr + filesz), 0, memsz - filesz);
            }
        }
    }

    *entry = hdr->entry;
    setup_user_stack(stack_top);

    kfree(elf_data);
    return 0;
}
