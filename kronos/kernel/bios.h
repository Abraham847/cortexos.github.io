#ifndef BIOS_H
#define BIOS_H

#include "kernel.h"

#define BIOS_MMAP_COUNT ((volatile u16*)0x2000)
#define BIOS_EXT_1_16   ((volatile u16*)0x2004)
#define BIOS_EXT_16PLUS ((volatile u16*)0x2006)
#define BIOS_CPUID_OK   ((volatile u8*)0x2008)
#define BIOS_CPUID_VEND ((volatile u8*)0x2010)

typedef struct {
    u32 base_lo;
    u32 base_hi;
    u32 len_lo;
    u32 len_hi;
    u32 type;
    u32 acpi_ext;
} __attribute__((packed)) bios_mmap_t;

#define BIOS_VBE_WIDTH   ((volatile u16*)0x2100)
#define BIOS_VBE_HEIGHT  ((volatile u16*)0x2102)
#define BIOS_VBE_BPP     ((volatile u8*)0x2106)
#define BIOS_VBE_ACTIVE  ((volatile u8*)0x2107)
#define BIOS_VBE_FB      ((volatile u32*)0x2108)
#define BIOS_VBE_PITCH   ((volatile u16*)0x210C)

#define BIOS_MMAP ((bios_mmap_t*)0x1000)

static inline u32 bios_total_mem_kb(void) {
    return *BIOS_EXT_1_16 + (u32)(*BIOS_EXT_16PLUS) * 64 + 1024;
}

static inline const char *bios_cpu_vendor(void) {
    return *BIOS_CPUID_OK ? (const char*)BIOS_CPUID_VEND : "Unknown";
}

#endif
