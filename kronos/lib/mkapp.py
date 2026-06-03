#!/usr/bin/env python3
"""Convert a flat binary into a CortexOS app (add checksum)."""
import sys, os

def mkapp(src, dst):
    with open(src, 'rb') as f:
        data = f.read()
    csum = 0
    for b in data:
        csum ^= b
    with open(dst, 'wb') as f:
        f.write(data)
        f.write(bytes([csum]))
    size = len(data) + 1
    print(f'{os.path.basename(dst)}: {size} bytes, csum={csum:02x}')

if __name__ == '__main__':
    if len(sys.argv) < 3:
        print(f'Usage: {sys.argv[0]} input.bin output.BIN')
        sys.exit(1)
    mkapp(sys.argv[1], sys.argv[2])
