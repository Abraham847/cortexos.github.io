@echo off
echo Creating 32MB FAT32 hard disk image...
wsl bash -c "dd if=/dev/zero bs=1M count=32 of=hdd.img 2>/dev/null"
wsl bash -c "echo 'label: dos' | /sbin/sfdisk hdd.img 2>/dev/null"
wsl bash -c "echo ',,c' | /sbin/sfdisk hdd.img -a 2>/dev/null || echo '(sfdisk might need adjustment)'"
echo Disk image created: hdd.img (32MB)
echo.
echo To run with HDD:
echo qemu-system-i386 -drive format=raw,file=cortexos.img -drive format=raw,file=hdd.img -netdev user,id=net0 -device rtl8139,netdev=net0 -vga std -m 128
