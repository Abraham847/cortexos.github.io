#!/bin/bash
DIR=/mnt/c/Users/Colibecas/Desktop/ios/kronos/kernel
CC=~/gcc-cross/bin/i686-linux-musl-gcc
FLAGS="-ffreestanding -nostdlib -nostartfiles -m32 -fno-pic -mno-red-zone -c"
echo "=== Compiling C files ==="
for f in kernel vga idt keyboard mouse timer window desktop shell ai nn heap ata fs editor forth paint task ipc model aidemo lang; do
  $CC $FLAGS $DIR/$f.c -o $DIR/$f.o 2>&1
  if [ $? -eq 0 ]; then echo "OK: $f"; else echo "FAIL: $f"; fi
done
echo "=== Assembling ==="
/tmp/nasm -f elf32 $DIR/entry.asm -o $DIR/entry.o && echo "OK: entry.asm" || echo "FAIL: entry.asm"
/tmp/nasm -f elf32 $DIR/isr.asm -o $DIR/isr.o && echo "OK: isr.asm" || echo "FAIL: isr.asm"
echo "ALL_DONE"
