#include "pci.h"
#include "core.h"

int pci_init(void) {
    u32 tmp = pci_read_cfg(0, 0, 0, 0);
    if (tmp == 0xFFFFFFFF) return -1;
    return 0;
}

u32 pci_read_cfg(u8 bus, u8 slot, u8 func, u8 offset) {
    u32 addr = 0x80000000
        | ((u32)bus << 16)
        | ((u32)slot << 11)
        | ((u32)func << 8)
        | (offset & 0xFC);
    outl(PCI_CONF_ADDR, addr);
    return inl(PCI_CONF_DATA);
}

void pci_write_cfg(u8 bus, u8 slot, u8 func, u8 offset, u32 val) {
    u32 addr = 0x80000000
        | ((u32)bus << 16)
        | ((u32)slot << 11)
        | ((u32)func << 8)
        | (offset & 0xFC);
    outl(PCI_CONF_ADDR, addr);
    outl(PCI_CONF_DATA, val);
}

int pci_scan(u16 vendor, u16 device, pci_dev_t *dev) {
    for (int bus = 0; bus < 256; bus++) {
        for (int slot = 0; slot < 32; slot++) {
            for (int func = 0; func < 8; func++) {
                u32 id = pci_read_cfg(bus, slot, func, 0);
                if (id == 0xFFFFFFFF) {
                    if (func == 0) break;
                    continue;
                }
                u16 vid = id & 0xFFFF;
                u16 did = (id >> 16) & 0xFFFF;
                if (vid == vendor && did == device) {
                    dev->vendor_id = vid;
                    dev->device_id = did;
                    dev->bus = bus;
                    dev->slot = slot;
                    dev->func = func;
                    u32 cc = pci_read_cfg(bus, slot, func, 8);
                    dev->class_code = (cc >> 24) & 0xFF;
                    dev->subclass = (cc >> 16) & 0xFF;
                    dev->prog_if = (cc >> 8) & 0xFF;
                    dev->bar0 = pci_read_cfg(bus, slot, func, 0x10);
                    dev->bar1 = pci_read_cfg(bus, slot, func, 0x14);
                    u32 irq_line = pci_read_cfg(bus, slot, func, 0x3C);
                    dev->irq = irq_line & 0xFF;
                    return 1;
                }
                if (func == 0) {
                    u32 hdr = pci_read_cfg(bus, slot, func, 0x0C);
                    if (!(hdr & 0x800000)) break;
                }
            }
        }
    }
    return 0;
}
