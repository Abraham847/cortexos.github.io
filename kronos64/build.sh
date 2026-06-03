#!/bin/bash
# KronOS64 Build Script
set -e

DIR="$(cd "$(dirname "$0")" && pwd)"
NASM="/tmp/nasm"
TMP="/tmp/kronos64_build"
mkdir -p "$TMP"

echo "=== KronOS64 Build ==="

# Assemble bootloader
echo "[1/3] Assembling bootloader..."
$NASM -f bin "$DIR/boot/boot.asm" -o "$TMP/boot.bin"
echo "OK: boot.bin ($(wc -c < "$TMP/boot.bin") bytes)"

# Assemble kernel
echo "[2/3] Assembling kernel..."
cd "$DIR/kernel"
$NASM -f bin kernel.asm -o "$TMP/kernel.bin" 2>&1
cd "$DIR"
echo "OK: kernel.bin ($(wc -c < "$TMP/kernel.bin") bytes)"

# Create disk image
echo "[3/3] Creating disk image..."
cp "$DIR/make_image.py" "$TMP/"
cd "$TMP"
python3 make_image.py
cp "$TMP/kronos64.img" "$DIR/kronos64.img"
cp "$TMP/kernel.bin" "$DIR/kernel.bin" 2>/dev/null || true
cp "$TMP/boot.bin" "$DIR/boot.bin" 2>/dev/null || true
cd "$DIR"
echo "OK: kronos64.img"

echo ""
echo "=== Done ==="
echo "Boot: $DIR/boot.bin ($(wc -c < "$DIR/boot.bin") bytes)"
echo "Kernel: $DIR/kernel.bin ($(wc -c < "$DIR/kernel.bin") bytes)"
echo "Image: $DIR/kronos64.img"
