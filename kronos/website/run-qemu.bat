@echo off
setlocal
title Kronos OS - QEMU

set QEMU=
for %%e in (
    "qemu-system-i386.exe"
    "qemu.exe"
    "qemu-system-x86_64.exe"
) do (
    where %%e >nul 2>&1 && set QEMU=%%~e
)

if "%QEMU%"=="" (
    echo [ERROR] QEMU not found.
    echo Install QEMU from https://www.qemu.org/download/
    pause
    exit /b 1
)

if not exist "cortexos.img" (
    if exist "..\kronos\cortexos.img" (
        copy "..\kronos\cortexos.img" "cortexos.img" >nul
    ) else (
        echo [ERROR] cortexos.img not found.
        pause
        exit /b 1
    )
)

echo Starting Kronos OS in QEMU... (press Ctrl+Alt+G to release mouse)
%QEMU% -fda "cortexos.img" -m 64 -serial stdio
pause
