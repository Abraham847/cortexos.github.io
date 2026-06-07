#!/bin/bash
set -e
DIR=/mnt/c/Users/Colibecas/Desktop/ios/kronos
CC=/home/olibecas/gcc-cross/bin/i686-linux-musl-gcc
I="-I$DIR/kernel -I$DIR/kernel/core -I$DIR/kernel/drivers -I$DIR/kernel/ui -I$DIR/kernel/ml -I$DIR/kernel/apps -I$DIR/kernel/arch"
FLAGS="-ffreestanding -nostdlib -nostartfiles -m32 -fno-pic -mno-red-zone -c $I"

echo "=== Cleaning old objects ==="
rm -f $DIR/kernel/*.o $DIR/kernel/core/*.o $DIR/kernel/drivers/*.o $DIR/kernel/ui/*.o $DIR/kernel/ml/*.o $DIR/kernel/apps/*.o $DIR/kernel.elf $DIR/kernel.bin $DIR/cortexos.img $DIR/boot.bin

echo "=== Compiling C files ==="
FILES="kernel \
  core/heap core/task core/idt core/ipc core/event_queue core/synch \
  arch/x86 \
  drivers/vga drivers/keyboard drivers/mouse drivers/timer drivers/ata drivers/fs \
  ui/desktop ui/window ui/shell ui/sysmon ui/fman ui/boot ui/editor ui/forth ui/paint ui/lang \
  ml/nn ml/ai ml/model ml/aidemo \
  apps/kernel_api apps/loader"

for f in $FILES; do
  $CC $FLAGS $DIR/kernel/$f.c -o $DIR/kernel/$f.o
  echo "OK: $f"
done

echo "=== Assembling ==="
cd $DIR
rm -f boot.bin kernel/entry.o kernel/isr.o
/tmp/nasm -f bin boot/boot.asm -o boot.bin
/tmp/nasm -f elf32 kernel/entry.asm -o kernel/entry.o
/tmp/nasm -f elf32 kernel/isr.asm -o kernel/isr.o

echo "=== Linking ==="
ld -m elf_i386 \
  -L /home/olibecas/gcc-cross/lib/gcc/i686-linux-musl/11.2.1 \
  -o $DIR/kernel.elf -T $DIR/kernel.ld -nostdlib \
  $DIR/kernel/entry.o $DIR/kernel/isr.o \
  $DIR/kernel/kernel.o \
  $DIR/kernel/core/heap.o $DIR/kernel/core/task.o $DIR/kernel/core/idt.o $DIR/kernel/core/ipc.o $DIR/kernel/core/event_queue.o $DIR/kernel/core/synch.o \
  $DIR/kernel/arch/x86.o \
  $DIR/kernel/drivers/vga.o $DIR/kernel/drivers/keyboard.o $DIR/kernel/drivers/mouse.o $DIR/kernel/drivers/timer.o $DIR/kernel/drivers/ata.o $DIR/kernel/drivers/fs.o \
  $DIR/kernel/ui/desktop.o $DIR/kernel/ui/window.o $DIR/kernel/ui/shell.o $DIR/kernel/ui/sysmon.o $DIR/kernel/ui/fman.o $DIR/kernel/ui/boot.o $DIR/kernel/ui/editor.o $DIR/kernel/ui/forth.o $DIR/kernel/ui/paint.o $DIR/kernel/ui/lang.o \
  $DIR/kernel/ml/nn.o $DIR/kernel/ml/ai.o $DIR/kernel/ml/model.o $DIR/kernel/ml/aidemo.o \
  $DIR/kernel/apps/kernel_api.o $DIR/kernel/apps/loader.o \
  -lgcc

echo "=== Binary ==="
objcopy -O binary $DIR/kernel.elf $DIR/kernel.bin

SIZE=$(stat -c%s "$DIR/kernel.bin")
echo "Kernel size: $SIZE bytes"

echo "=== Image ==="
python3 $DIR/build_img.py $DIR/kernel.bin $DIR/boot.bin $DIR/cortexos.img $DIR/xordset.bin

echo "=== Verifying PIC fix ==="
objdump -d $DIR/kernel.elf | grep -F 'push' | grep -E '0xfc|0xf8' || echo "PIC push not found (may be optimized out)"

echo "=== Verifying mode 13h ==="
python3 -c "
with open('$DIR/kernel.bin','rb') as f: d=f.read()
for i in range(len(d)-5):
    if d[i]==0xB8 and d[i+1]==0x13 and d[i+2]==0x00 and d[i+3]==0xCD and d[i+4]==0x10:
        print('Mode 13h (mov ax,0x13; int 0x10) at byte', i)
        break
else:
    for i in range(len(d)-6):
        if d[i]==0x66 and d[i+1]==0xB8 and d[i+2]==0x13 and d[i+3]==0x00 and d[i+4]==0x00 and d[i+5]==0xCD and d[i+6]==0x10:
            print('Mode 13h with 66 prefix at byte', i)
            break
    else:
        print('Mode 13h NOT FOUND in kernel.bin')
"

python3 $DIR/check_fat.py
echo "=== DONE ==="
