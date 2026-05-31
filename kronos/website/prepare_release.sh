#!/bin/bash
# Prepare release artifacts for the website
# Run from the kronos root directory after building
set -e

DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(dirname "$DIR")"

echo "=== Kronos OS Release Preparer ==="

# Check that the image exists
if [ ! -f "$ROOT/cortexos.img" ]; then
    echo "ERROR: cortexos.img not found. Run build_full.sh first."
    exit 1
fi

echo "[1/4] Copying cortexos.img to website/"
cp "$ROOT/cortexos.img" "$DIR/cortexos.img"

echo "[2/4] Creating cortexos.vdi..."
if command -v VBoxManage &>/dev/null; then
    VBoxManage convertfromraw "$DIR/cortexos.img" "$DIR/cortexos.vdi" --format VDI
elif command -v python3 &>/dev/null; then
    python3 "$DIR/img2vdi.py" "$DIR/cortexos.img" "$DIR/cortexos.vdi"
else
    echo "WARN: Neither VBoxManage nor Python3 found. Skipping VDI."
fi

echo "[3/4] Creating source archive..."
cd "$ROOT"
if command -v git &>/dev/null; then
    git archive --format=zip --output="$DIR/kronos-src.zip" HEAD 2>/dev/null || \
    zip -r "$DIR/kronos-src.zip" . -x "*.git*" "*.o" "*.elf" "*.bin" "*.img" "website/cortexos.*" "website/kronos-src.zip" >/dev/null
else
    zip -r "$DIR/kronos-src.zip" . -x "*.git*" "*.o" "*.elf" "*.bin" "*.img" "website/cortexos.*" "website/kronos-src.zip" >/dev/null
fi

echo "[4/4] Done."
echo ""
echo "Website files:"
ls -lh "$DIR/cortexos.img" "$DIR/cortexos.vdi" "$DIR/kronos-src.zip" 2>/dev/null
echo ""
echo "Website is ready at: $DIR"
echo "Upload the entire website/ directory to your hosting."
