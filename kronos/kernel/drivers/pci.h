#ifndef PCI_H
#define PCI_H

#include "core.h"

#define PCI_CONF_ADDR 0xCF8
#define PCI_CONF_DATA 0xCFC

typedef struct {
    u16 vendor_id;
    u16 device_id;
    u8 bus;
    u8 slot;
    u8 func;
    u8 class_code;
    u8 subclass;
    u8 prog_if;
    u32 bar0;
    u32 bar1;
    u32 irq;
} pci_dev_t;

int pci_init(void);
int pci_scan(u16 vendor, u16 device, pci_dev_t *dev);
u32 pci_read_cfg(u8 bus, u8 slot, u8 func, u8 offset);
void pci_write_cfg(u8 bus, u8 slot, u8 func, u8 offset, u32 val);

#endif
