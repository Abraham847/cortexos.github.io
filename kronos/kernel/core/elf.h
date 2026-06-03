#ifndef ELF_H
#define ELF_H

#include "core.h"

#define ELF_MAGIC 0x464C457F

#define PT_NULL    0
#define PT_LOAD    1
#define PT_DYNAMIC 2
#define PT_INTERP  3
#define PT_NOTE    4
#define PT_PHDR    6

typedef struct {
    u32 magic;
    u8  cls;
    u8  data;
    u8  version;
    u8  osabi;
    u8  abiver;
    u8  pad[7];
    u16 type;
    u16 machine;
    u32 version2;
    u32 entry;
    u32 phoff;
    u32 shoff;
    u32 flags;
    u16 ehsize;
    u16 phentsize;
    u16 phnum;
    u16 shentsize;
    u16 shnum;
    u16 shstrndx;
} __attribute__((packed)) elf32_hdr_t;

typedef struct {
    u32 type;
    u32 offset;
    u32 vaddr;
    u32 paddr;
    u32 filesz;
    u32 memsz;
    u32 flags;
    u32 align;
} __attribute__((packed)) elf32_phdr_t;

int elf_load(const char *path, u32 *entry, u32 *stack_top);

#endif
