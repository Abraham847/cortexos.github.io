#!/bin/bash
set -e
DIR=/mnt/c/Users/Colibecas/Desktop/ios/kronos
CC=/home/olibecas/gcc-cross/bin/i686-linux-musl-gcc
FLAGS="-ffreestanding -nostdlib -nostartfiles -m32 -fno-pic -mno-red-zone -c"

echo "=== Cleaning old objects ==="
rm -f $DIR/kernel/*.o $DIR/kernel.elf $DIR/kernel.bin $DIR/cortexos.img $DIR/boot.bin

echo "=== Compiling C files ==="
for f in kernel vga idt keyboard mouse timer window desktop shell ai nn heap ata fs editor forth paint task ipc model aidemo lang; do
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
ld -m elf_i386 $DIR/kernel/entry.o $DIR/kernel/isr.o \
  $DIR/kernel/kernel.o $DIR/kernel/vga.o $DIR/kernel/idt.o \
  $DIR/kernel/keyboard.o $DIR/kernel/mouse.o $DIR/kernel/timer.o \
  $DIR/kernel/window.o $DIR/kernel/desktop.o $DIR/kernel/shell.o \
  $DIR/kernel/ai.o $DIR/kernel/nn.o $DIR/kernel/heap.o \
  $DIR/kernel/ata.o $DIR/kernel/fs.o $DIR/kernel/editor.o \
  $DIR/kernel/forth.o $DIR/kernel/paint.o $DIR/kernel/task.o \
  $DIR/kernel/ipc.o $DIR/kernel/model.o $DIR/kernel/aidemo.o \
  $DIR/kernel/lang.o \
  -T $DIR/kernel.ld -o $DIR/kernel.elf -nostdlib

echo "=== Binary ==="
objcopy -O binary $DIR/kernel.elf $DIR/kernel.bin
SIZE=$(stat -c%s "$DIR/kernel.bin")
echo "Kernel size: $SIZE bytes"

echo "=== Image ==="
python3 $DIR/build_img.py $DIR/kernel.bin $DIR/boot.bin $DIR/cortexos.img $DIR/xordset.bin

echo "=== Verifying PIC fix ==="
objdump -d $DIR/kernel.elf | grep -F 'push' | grep -E '0xfc|0xf8'

echo "=== Verifying mode 13h ==="
python3 -c "
with open('$DIR/kernel.bin','rb') as f: d=f.read()
for i in range(len(d)-5):
    if d[i]==0xB8 and d[i+1]==0x13 and d[i+2]==0x00 and d[i+3]==0xCD and d[i+4]==0x10:
        print('Mode 13h (mov ax,0x13; int 0x10) at byte', i)
        break
else:
    # Try with 66 prefix (32-bit in 16-bit mode)
    for i in range(len(d)-6):
        if d[i]==0x66 and d[i+1]==0xB8 and d[i+2]==0x13 and d[i+3]==0x00 and d[i+4]==0x00 and d[i+5]==0xCD and d[i+6]==0x10:
            print('Mode 13h with 66 prefix at byte', i)
            break
    else:
        print('Mode 13h NOT FOUND in kernel.bin - checking in elf...')
        # Might be before start symbol
"

python3 $DIR/check_fat.py
echo "=== DONE ==="
