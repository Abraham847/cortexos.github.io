@echo off
setlocal
set GCC=~/gcc-cross/bin/i686-linux-musl-gcc
set WDIR=/mnt/c/Users/Colibecas/Desktop/ios/kronos
set SRC=%CD%
set APPNAME=%~n1

echo === Building %APPNAME% for CortexOS ===

REM Compile
wsl bash -c "cd '%WDIR%' && %GCC% -ffreestanding -nostdlib -m32 -fno-pic -mno-red-zone -c lib/crt0.c -I lib/libc -I kernel -I kernel/core -I kernel/drivers -I kernel/ui -o lib/crt0.o 2>&1"
wsl bash -c "cd '%WDIR%' && %GCC% -ffreestanding -nostdlib -m32 -fno-pic -mno-red-zone -c lib/libc.c -I lib/libc -I kernel -I kernel/core -I kernel/drivers -I kernel/ui -o lib/libc.o 2>&1"
wsl bash -c "cd '%WDIR%' && %GCC% -ffreestanding -nostdlib -m32 -fno-pic -mno-red-zone -c %1 -I lib/libc -I kernel -I kernel/core -I kernel/drivers -I kernel/ui -o lib/app.o 2>&1"

if errorlevel 1 exit /b 1

REM Link as flat binary
wsl bash -c "cd '%WDIR%' && %GCC% -ffreestanding -nostdlib -m32 -no-pie -Wl,--oformat,binary -Wl,-Ttext,0x1000 -o lib/%APPNAME%.bin lib/crt0.o lib/libc.o lib/app.o 2>&1"

if errorlevel 1 exit /b 1

echo App built: lib/%APPNAME%.bin
echo To copy to disk: copy lib\%APPNAME%.bin hdd:\
