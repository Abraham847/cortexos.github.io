#!/bin/bash
DIR=/mnt/c/Users/Colibecas/Desktop/ios/kronos
CC=~/gcc-cross/bin/i686-linux-musl-gcc

echo "=== Recompiling idt.c ==="
$CC -ffreestanding -nostdlib -nostartfiles -m32 -fno-pic -mno-red-zone -c $DIR/kernel/idt.c -o $DIR/kernel/idt.o 2>&1
echo "Exit: $?"

echo "=== Checking for 0xF8 (should be present) and 0xFC (should be absent) ==="
objdump -d $DIR/kernel/idt.o 2>/dev/null | grep -E "push.*0xfc|push.*0xf8"
echo "=== Done ==="
