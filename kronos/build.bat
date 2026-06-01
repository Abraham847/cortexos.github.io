@echo off
setlocal
set NASM=%TEMP%\nasm\nasm-2.16.01\nasm.exe
set GCC=~/gcc-cross/bin/i686-linux-musl-gcc
set SRC=%CD%
set WDIR=/mnt/c/Users/Colibecas/Desktop/ios/kronos

echo === Building CortexOS (restructured) ===

echo [1/5] Bootloader...
wsl bash -c "cd '%WDIR%' && /tmp/nasm -f bin boot/boot.asm -o boot.bin"
if errorlevel 1 exit /b 1

echo [2/5] Assembly stubs...
wsl bash -c "cd '%WDIR%/kernel' && /tmp/nasm -f elf32 entry.asm -o entry.o"
if errorlevel 1 exit /b 1
wsl bash -c "cd '%WDIR%/kernel' && /tmp/nasm -f elf32 isr.asm -o isr.o"
if errorlevel 1 exit /b 1

echo [3/5] C kernel...
set I=-I%WDIR%/kernel -I%WDIR%/kernel/core -I%WDIR%/kernel/drivers -I%WDIR%/kernel/ui -I%WDIR%/kernel/ml -I%WDIR%/kernel/apps
set FLAGS=-ffreestanding -nostdlib -nostartfiles -m32 -fno-pic -mno-red-zone -c
for %%f in (kernel.c) do (
    wsl bash -c "cd '%WDIR%/kernel' && %GCC% %FLAGS% %I% %%f -o %%~nf.o"
    if errorlevel 1 echo %%f FAILED && exit /b 1
)
for %%f in (core/heap.c core/task.c core/idt.c core/ipc.c core/event_queue.c) do (
    wsl bash -c "cd '%WDIR%/kernel' && %GCC% %FLAGS% %I% %%f -o core/%%~nf.o"
    if errorlevel 1 echo %%f FAILED && exit /b 1
)
for %%f in (drivers/vga.c drivers/keyboard.c drivers/mouse.c drivers/timer.c drivers/ata.c drivers/fs.c) do (
    wsl bash -c "cd '%WDIR%/kernel' && %GCC% %FLAGS% %I% %%f -o drivers/%%~nf.o"
    if errorlevel 1 echo %%f FAILED && exit /b 1
)
for %%f in (ui/desktop.c ui/window.c ui/shell.c ui/sysmon.c ui/fman.c ui/boot.c ui/editor.c ui/forth.c ui/paint.c ui/lang.c) do (
    wsl bash -c "cd '%WDIR%/kernel' && %GCC% %FLAGS% %I% %%f -o ui/%%~nf.o"
    if errorlevel 1 echo %%f FAILED && exit /b 1
)
for %%f in (ml/nn.c ml/ai.c ml/model.c ml/aidemo.c) do (
    wsl bash -c "cd '%WDIR%/kernel' && %GCC% %FLAGS% %I% %%f -o ml/%%~nf.o"
    if errorlevel 1 echo %%f FAILED && exit /b 1
)
for %%f in (apps/kernel_api.c apps/loader.c) do (
    wsl bash -c "cd '%WDIR%/kernel' && %GCC% %FLAGS% %I% %%f -o apps/%%~nf.o"
    if errorlevel 1 echo %%f FAILED && exit /b 1
)

echo [4/5] Linking...
wsl bash -c "cd '%WDIR%' && %GCC% -ffreestanding -nostdlib -nostartfiles -m32 -no-pie -Wl,-T -Wl,kernel.ld -Wl,--oformat,binary -o kernel.bin kernel/entry.o kernel/isr.o kernel/kernel.o kernel/core/heap.o kernel/core/task.o kernel/core/idt.o kernel/core/ipc.o kernel/core/event_queue.o kernel/drivers/vga.o kernel/drivers/keyboard.o kernel/drivers/mouse.o kernel/drivers/timer.o kernel/drivers/ata.o kernel/drivers/fs.o kernel/ui/desktop.o kernel/ui/window.o kernel/ui/shell.o kernel/ui/sysmon.o kernel/ui/fman.o kernel/ui/boot.o kernel/ui/editor.o kernel/ui/forth.o kernel/ui/paint.o kernel/ui/lang.o kernel/ml/nn.o kernel/ml/ai.o kernel/ml/model.o kernel/ml/aidemo.o kernel/apps/kernel_api.o kernel/apps/loader.o"

if errorlevel 1 exit /b 1

echo [5/5] Creating disk image...
wsl bash -c "cd '%WDIR%' && cat boot.bin kernel.bin > cortexos.img && dd if=/dev/zero bs=512 count=2880 2>/dev/null | tr '\000' '\377' > floppy.img && dd if=boot.bin of=floppy.img bs=512 count=1 conv=notrunc 2>/dev/null && dd if=kernel.bin of=floppy.img bs=512 seek=1 conv=notrunc 2>/dev/null"

echo === CortexOS built! (restructured) ===
echo Run: qemu-system-i386 -drive format=raw,file=cortexos.img
