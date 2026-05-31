#!/usr/bin/env python3
"""Convert raw IMG to VirtualBox VDI (fixed-size, floppy-compatible)."""
import os, struct, sys, uuid

_BLK = 0x100000  # 1 MB block size
_SS  = 512       # sector size

def img2vdi(img_path, vdi_path):
    with open(img_path, 'rb') as f:
        raw = f.read()
    # Pad to sector boundary
    if len(raw) % _SS:
        raw += b'\x00' * (_SS - len(raw) % _SS)
    sz = len(raw)
    blocks = (sz + _BLK - 1) // _BLK
    bmap_off = 512                      # block map right after header
    data_off = (bmap_off + blocks * 4 + _SS - 1) & ~(_SS - 1)  # page-align
    total_blocks = blocks

    hdr = bytearray(512)
    def w32(off, v): struct.pack_into('<I', hdr, off, v)

    w32(0x00, 0x00010176)   # magic
    w32(0x04, 0x00010001)   # version 1.1
    w32(0x08, 512)           # header size
    w32(0x0C, 1)             # image type: 1=fixed
    w32(0x10, 0)             # flags
    w32(0x14, 0)             # reserved
    # CHS geometry
    hd = 2
    sp  = 18
    cy  = (sz // _SS) // (hd * sp)
    w32(0x18, cy)            # cylinders
    w32(0x1C, hd)            # heads
    w32(0x20, sp)            # sectors/track
    w32(0x24, _SS)           # sector size
    w32(0x28, 0)
    struct.pack_into('<Q', hdr, 0x2C, sz)   # image size (8 bytes)
    w32(0x34, _BLK)          # block size
    w32(0x38, 0)             # block extra data
    w32(0x3C, total_blocks)  # total blocks
    w32(0x40, total_blocks)  # allocated blocks
    struct.pack_into('<16s', hdr, 0x48, uuid.uuid4().bytes)  # UUID creator
    struct.pack_into('<16s', hdr, 0x58, uuid.uuid4().bytes)  # UUID data (modification)
    struct.pack_into('<16s', hdr, 0x68, uuid.uuid4().bytes)  # UUID linkage
    w32(0x70, bmap_off)      # block map offset
    w32(0x74, 0)             # unused
    w32(0x78, 0)
    w32(0x7C, 0)
    w32(0x80, total_blocks)  # size of block map (blocks)
    w32(0x84, blocks)        # size of data blocks

    with open(vdi_path, 'wb') as f:
        f.write(hdr)
        # Block map: one offset per block
        for i in range(blocks):
            f.write(struct.pack('<I', data_off + i * _BLK))
        # Pad to data offset
        pos = bmap_off + blocks * 4
        if pos < data_off:
            f.write(b'\x00' * (data_off - pos))
        # Data
        f.write(raw)
        # Pad last block
        if sz % _BLK:
            f.write(b'\x00' * (_BLK - sz % _BLK))

    print(f"OK: {img_path} ({sz} bytes) -> {vdi_path}")
    print(f"    Blocks: {blocks}, Image size: {sz}")

if __name__ == '__main__':
    import sys
    if len(sys.argv) < 2:
        print("Usage: python img2vdi.py input.img [output.vdi]")
        sys.exit(1)
    img = sys.argv[1]
    vdi = sys.argv[2] if len(sys.argv) > 2 else img.rsplit('.', 1)[0] + '.vdi'
    img2vdi(img, vdi)
