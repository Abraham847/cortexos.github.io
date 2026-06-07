@echo off
setlocal
set GCC=~/gcc-cross/bin/i686-linux-musl-gcc
set SRC=%CD%
set WDIR=/mnt/c/Users/Colibecas/Desktop/ios/kronos
set NASM_WSL=/mnt/c/Users/Colibecas/AppData/Local/Temp/nasm/nasm-2.16.01/nasm.exe

echo === Building CortexOS (restructured) ===

echo [1/5] Bootloader...
wsl bash -c "cd '%WDIR%' && '%NASM_WSL%' -f bin boot/boot.asm -o boot.bin"
if errorlevel 1 exit /b 1

echo [2/5] Assembly stubs...
wsl bash -c "cd '%WDIR%/kernel' && '%NASM_WSL%' -f elf32 entry.asm -o entry.o"
if errorlevel 1 exit /b 1
wsl bash -c "cd '%WDIR%/kernel' && '%NASM_WSL%' -f elf32 isr.asm -o isr.o"
if errorlevel 1 exit /b 1

echo [3/5] C kernel...
set I=-I%WDIR%/kernel -I%WDIR%/kernel/core -I%WDIR%/kernel/drivers -I%WDIR%/kernel/ui -I%WDIR%/kernel/ml -I%WDIR%/kernel/apps -I%WDIR%/kernel/arch -I%WDIR%/kernel/fs -I%WDIR%/kernel/net
set FLAGS=-ffreestanding -nostdlib -nostartfiles -m32 -fno-pic -mno-red-zone -c
for %%f in (kernel.c) do (
    wsl bash -c "cd '%WDIR%/kernel' && %GCC% %FLAGS% %I% %%f -o %%~nf.o"
    if errorlevel 1 echo %%f FAILED && exit /b 1
)
for %%f in (core/heap.c core/idt.c core/ipc.c core/event_queue.c core/synch.c core/vfs.c core/syscall.c core/process.c core/elf.c) do (
    wsl bash -c "cd '%WDIR%/kernel' && %GCC% %FLAGS% %I% %%f -o core/%%~nf.o"
    if errorlevel 1 echo %%f FAILED && exit /b 1
)
for %%f in (arch/x86.c arch/paging.c) do (
    wsl bash -c "cd '%WDIR%/kernel' && %GCC% %FLAGS% %I% %%f -o arch/%%~nf.o"
    if errorlevel 1 echo %%f FAILED && exit /b 1
)
for %%f in (drivers/vga.c drivers/keyboard.c drivers/mouse.c drivers/timer.c drivers/ata.c drivers/fs.c drivers/pci.c drivers/rtl8139.c drivers/rtc.c drivers/sb16.c) do (
    wsl bash -c "cd '%WDIR%/kernel' && %GCC% %FLAGS% %I% %%f -o drivers/%%~nf.o"
    if errorlevel 1 echo %%f FAILED && exit /b 1
)
for %%f in (ui/desktop.c ui/window.c ui/shell.c ui/sysmon.c ui/fman.c ui/boot.c ui/editor.c ui/forth.c ui/paint.c ui/lang.c ui/ailab.c ui/aicreator.c ui/aistudio.c ui/nnview.c) do (
    wsl bash -c "cd '%WDIR%/kernel' && %GCC% %FLAGS% %I% %%f -o ui/%%~nf.o"
    if errorlevel 1 echo %%f FAILED && exit /b 1
)
for %%f in (ml/nn.c ml/ai.c ml/model.c ml/aidemo.c ml/kpu.c ml/nn_float.c) do (
    wsl bash -c "cd '%WDIR%/kernel' && %GCC% %FLAGS% %I% %%f -o ml/%%~nf.o"
    if errorlevel 1 echo %%f FAILED && exit /b 1
)
for %%f in (apps/kernel_api.c apps/loader.c) do (
    wsl bash -c "cd '%WDIR%/kernel' && %GCC% %FLAGS% %I% %%f -o apps/%%~nf.o"
    if errorlevel 1 echo %%f FAILED && exit /b 1
)
for %%f in (net/net.c net/tcp.c net/dns.c net/dhcp.c) do (
    wsl bash -c "cd '%WDIR%/kernel' && %GCC% %FLAGS% %I% %%f -o net/%%~nf.o"
    if errorlevel 1 echo %%f FAILED && exit /b 1
)
for %%f in (fs/fatvfs.c fs/procfs.c fs/devfs.c fs/tmpfs.c) do (
    wsl bash -c "cd '%WDIR%/kernel' && %GCC% %FLAGS% %I% %%f -o fs/%%~nf.o"
    if errorlevel 1 echo %%f FAILED && exit /b 1
)

echo [4/5] Linking...
wsl bash -c "cd '%WDIR%' && %GCC% -ffreestanding -nostdlib -nostartfiles -m32 -no-pie -Wl,-T -Wl,kernel.ld -Wl,--oformat,binary -o kernel.bin kernel/entry.o kernel/isr.o kernel/kernel.o kernel/core/heap.o kernel/core/idt.o kernel/core/ipc.o kernel/core/event_queue.o kernel/core/synch.o kernel/core/vfs.o kernel/core/syscall.o kernel/core/process.o kernel/core/elf.o kernel/arch/x86.o kernel/arch/paging.o kernel/drivers/vga.o kernel/drivers/keyboard.o kernel/drivers/mouse.o kernel/drivers/timer.o kernel/drivers/ata.o kernel/drivers/fs.o kernel/drivers/pci.o kernel/drivers/rtl8139.o kernel/drivers/rtc.o kernel/drivers/sb16.o kernel/net/net.o kernel/net/tcp.o kernel/net/dns.o kernel/net/dhcp.o kernel/fs/fatvfs.o kernel/fs/procfs.o kernel/fs/devfs.o kernel/fs/tmpfs.o kernel/ui/desktop.o kernel/ui/window.o kernel/ui/shell.o kernel/ui/sysmon.o kernel/ui/fman.o kernel/ui/boot.o kernel/ui/editor.o kernel/ui/forth.o kernel/ui/paint.o kernel/ui/lang.o kernel/ui/ailab.o kernel/ui/aicreator.o kernel/ui/aistudio.o kernel/ui/nnview.o kernel/ml/nn.o kernel/ml/ai.o kernel/ml/model.o kernel/ml/aidemo.o kernel/ml/kpu.o kernel/ml/nn_float.o kernel/apps/kernel_api.o kernel/apps/loader.o -lgcc"

if errorlevel 1 exit /b 1

echo [5/5] Creating disk images...
wsl bash -c "cd '%WDIR%' && cat boot.bin kernel.bin > cortexos.img && dd if=/dev/zero bs=512 count=2880 2>/dev/null | tr '\000' '\377' > floppy.img && dd if=boot.bin of=floppy.img bs=512 count=1 conv=notrunc 2>/dev/null && dd if=kernel.bin of=floppy.img bs=512 seek=1 conv=notrunc 2>/dev/null"
echo Patching kernel size and recreating cortexos.img...
powershell -ExecutionPolicy Bypass -File "%~dp0patch_build.ps1"

echo === CortexOS built! ===
echo Run (HDD): qemu-system-i386 -drive file=cortexos.img -netdev user,id=net0 -device rtl8139,netdev=net0 -vga std -m 128 -audiodev sdl,id=audio0 -soundhw sb16
