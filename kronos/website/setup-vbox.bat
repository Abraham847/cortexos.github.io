@echo off
setlocal
title Kronos OS - VirtualBox Setup

echo ========================================
echo   Kronos OS v0.1 - VirtualBox Setup
echo ========================================
echo.

where VBoxManage >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo [ERROR] VBoxManage not found. Install VirtualBox first.
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

echo [1/4] Converting image...
VBoxManage convertfromraw cortexos.img cortexos.vdi --format VDI >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo [WARN] VDI conversion failed. Will try attaching raw .img.
)

set VM_NAME=KronOS
echo [2/4] Creating VM...
VBoxManage createvm --name "%VM_NAME%" --ostype "Other" --register >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo [INFO] VM may already exist. Updating configuration...
)

echo [3/4] Configuring...
VBoxManage modifyvm "%VM_NAME%" --memory 64 --vram 8 --ioapic on --acpi off >nul 2>&1
VBoxManage modifyvm "%VM_NAME%" --boot1 floppy --firmware bios >nul 2>&1
VBoxManage modifyvm "%VM_NAME%" --usb off --audio none --nic none >nul 2>&1

echo [4/4] Attaching disk...
VBoxManage storagectl "%VM_NAME%" --name "Floppy" --add floppy --controller I82078 >nul 2>&1
if exist "cortexos.vdi" (
    VBoxManage storageattach "%VM_NAME%" --storagectl "Floppy" --port 0 --device 0 --type fdd --medium "%CD%\cortexos.vdi" >nul 2>&1
)
if %ERRORLEVEL% neq 0 (
    VBoxManage storageattach "%VM_NAME%" --storagectl "Floppy" --port 0 --device 0 --type fdd --medium "%CD%\cortexos.img" >nul 2>&1
)

echo.
echo ========================================
echo   Done! VM "%VM_NAME%" is ready.
echo ========================================
echo.
echo   Start:  VBoxManage startvm "%VM_NAME%"
echo   Or open VirtualBox Manager and start "%VM_NAME%".
echo.
pause
