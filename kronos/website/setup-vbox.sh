#!/bin/bash
# Kronos OS v0.1 - VirtualBox Setup
set -e

VM_NAME="KronOS"
IMG="cortexos.img"

echo "=== Kronos OS v0.1 - VirtualBox Setup ==="

if [ ! -f "$IMG" ]; then
    if [ -f "../kronos/$IMG" ]; then
        cp "../kronos/$IMG" "$IMG"
    else
        echo "ERROR: $IMG not found. Run build_full.sh first or copy cortexos.img here."
        exit 1
    fi
fi

echo "[1/4] Checking dependencies..."
command -v VBoxManage >/dev/null 2>&1 || { echo "ERROR: VBoxManage not found."; exit 1; }

echo "[2/4] Converting to VDI..."
VBoxManage convertfromraw "$IMG" cortexos.vdi --format VDI 2>/dev/null && echo "  VDI created." || echo "  Will attach raw .img instead."

echo "[3/4] Creating VM..."
VBoxManage createvm --name "$VM_NAME" --ostype "Other" --register 2>/dev/null || true

echo "[4/4] Configuring VM..."
VBoxManage modifyvm "$VM_NAME" --memory 64 --vram 8 --ioapic on --acpi off 2>/dev/null || true
VBoxManage modifyvm "$VM_NAME" --boot1 floppy --firmware bios 2>/dev/null || true
VBoxManage modifyvm "$VM_NAME" --usb off --audio none --nic none 2>/dev/null || true
VBoxManage storagectl "$VM_NAME" --name "Floppy" --add floppy --controller I82078 2>/dev/null || true

MEDIUM="$PWD/cortexos.vdi"
if [ ! -f "$MEDIUM" ]; then MEDIUM="$PWD/$IMG"; fi
VBoxManage storageattach "$VM_NAME" --storagectl "Floppy" --port 0 --device 0 --type fdd --medium "$MEDIUM" 2>/dev/null || true

echo ""
echo "=== Done ==="
echo "VM '$VM_NAME' is ready."
echo "Start: VBoxManage startvm '$VM_NAME'"
