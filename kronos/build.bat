@echo off
setlocal
set NASM=%TEMP%\nasm\nasm-2.16.01\nasm.exe
set GCC=~/gcc-cross/bin/i686-linux-musl-gcc
set SRC=%CD%
set WDIR=/mnt/c/Users/Colibecas/Desktop/ios/kronos

echo === Building CortexOS ===

echo [1/5] Bootloader...
wsl bash -c "cd '%WDIR%' && /tmp/nasm -f bin boot/boot.asm -o boot.bin"
if errorlevel 1 exit /b 1

echo [2/5] Assembly stubs...
wsl bash -c "cd '%WDIR%/kernel' && /tmp/nasm -f elf32 entry.asm -o entry.o"
if errorlevel 1 exit /b 1
wsl bash -c "cd '%WDIR%/kernel' && /tmp/nasm -f elf32 isr.asm -o isr.o"
if errorlevel 1 exit /b 1

echo [3/5] C kernel...
for %%f in (kernel.c vga.c idt.c keyboard.c mouse.c timer.c window.c desktop.c shell.c ai.c) do (
    wsl bash -c "cd '%WDIR%/kernel' && ~/gcc-cross/bin/i686-linux-musl-gcc -ffreestanding -nostdlib -nostartfiles -m32 -fno-pic -mno-red-zone -c %%f -o %%~nf.o 2>&1"
    if errorlevel 1 echo %%f FAILED && exit /b 1
)

echo [4/5] Linking...
wsl bash -c "cd '%WDIR%' && ~/gcc-cross/bin/i686-linux-musl-gcc -ffreestanding -nostdlib -nostartfiles -m32 -no-pie -Wl,-T -Wl,kernel.ld -Wl,--oformat,binary -o kernel.bin kernel/entry.o kernel/isr.o kernel/kernel.o kernel/vga.o kernel/idt.o kernel/keyboard.o kernel/mouse.o kernel/timer.o kernel/window.o kernel/desktop.o kernel/shell.o kernel/ai.o 2>&1"
if errorlevel 1 exit /b 1

echo [5/5] Creating disk image...
wsl bash -c "cd '%WDIR%' && cat boot.bin kernel.bin > cortexos.img && dd if=/dev/zero bs=512 count=2880 2>/dev/null | tr '\000' '\377' > floppy.img && dd if=boot.bin of=floppy.img bs=512 count=1 conv=notrunc 2>/dev/null && dd if=kernel.bin of=floppy.img bs=512 seek=1 conv=notrunc 2>/dev/null"

echo === CortexOS built! ===
echo Run: qemu-system-i386 -drive format=raw,file=cortexos.img
