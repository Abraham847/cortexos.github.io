#!/bin/bash
echo "Creating 32MB FAT32 hard disk image..."
dd if=/dev/zero bs=1M count=32 of=hdd.img 2>/dev/null
echo "
o
n
p
1

t
c
w
" | fdisk hdd.img 2>/dev/null
echo "Formatting as FAT32..."
LOOP=$(sudo losetup -f --show hdd.img)
sudo mkfs.fat -F 32 "$LOOP" 2>/dev/null
sudo losetup -d "$LOOP"
echo "Done: hdd.img (32MB FAT32)"
echo ""
echo "Run with:"
echo "qemu-system-i386 -drive format=raw,file=cortexos.img -drive format=raw,file=hdd.img -netdev user,id=net0 -device rtl8139,netdev=net0 -vga std -m 128"
