#!/usr/bin/env python3
"""Create FAT12 floppy image with KERNEL.BIN"""
import struct, os, shutil

DIR = os.path.dirname(os.path.abspath(__file__))
KERNEL_SRC = os.path.join(DIR, "kernel.bin")
BOOT_SRC = os.path.join(DIR, "boot.bin")
OUTPUT = os.path.join(DIR, "kronos64.img")

FAT_SECTORS = 9
ROOT_ENTRIES = 224
ROOT_SECTORS = (ROOT_ENTRIES * 32 + 511) // 512
TOTAL_SECTORS = 2880
SECTORS_PER_CLUSTER = 1
BYTES_PER_SECTOR = 512

# Read kernel
with open(KERNEL_SRC, "rb") as f:
    kernel_data = f.read()
kernel_size = len(kernel_data)
kernel_sectors = (kernel_size + BYTES_PER_SECTOR - 1) // BYTES_PER_SECTOR

if kernel_sectors > 200:
    print(f"ERROR: Kernel too large ({kernel_sectors} sectors, max ~200)")
    exit(1)

# Calculate layout
fat_start = 1
root_start = fat_start + FAT_SECTORS
data_start = root_start + ROOT_SECTORS

# Create empty image
image = bytearray(TOTAL_SECTORS * BYTES_PER_SECTOR)

# Boot sector
with open(BOOT_SRC, "rb") as f:
    boot = f.read()
if len(boot) > 512:
    print("ERROR: Bootloader too large")
    exit(1)
image[0:len(boot)] = boot

# Patch BPB values for FAT12
# Bytes per sector
image[11:13] = struct.pack("<H", BYTES_PER_SECTOR)
image[13] = SECTORS_PER_CLUSTER
# Reserved sectors
image[14:16] = struct.pack("<H", fat_start)
image[16] = FAT_SECTORS  # FAT count
image[17:19] = struct.pack("<H", ROOT_ENTRIES)
image[19:21] = struct.pack("<H", TOTAL_SECTORS)
image[21] = 0xF0  # media descriptor
image[22] = 0  # FAT12: sectors per FAT (auto)

# Set FAT12 signature for 1.44MB
image[21] = 0xF0
image[22:24] = struct.pack("<H", FAT_SECTORS)  # sectors per FAT
image[24:26] = struct.pack("<H", SECTORS_PER_CLUSTER * 2)  # sectors per track
image[26:28] = struct.pack("<H", 80)  # heads
image[28:32] = struct.pack("<I", 0)  # hidden sectors

# Fill FAT
# FAT[0] = media descriptor + 0xFFF
image[fat_start*BYTES_PER_SECTOR : fat_start*BYTES_PER_SECTOR+3] = bytes([0xF0, 0xFF, 0xFF])

# FAT entries for kernel clusters
clusters = kernel_sectors
for i in range(1, clusters + 1):
    entry = i + 1 if i < clusters else 0xFFF
    off = fat_start * BYTES_PER_SECTOR + (i * 3 // 2)
    if i % 2 == 0:
        val = (entry & 0xFFF) | (image[off] << 8)
        image[off] = val & 0xFF
        image[off+1] = (val >> 8) & 0xFF
    else:
        val = image[off] | ((entry & 0xFFF) << 4)
        image[off] = val & 0xFF
        image[off+1] = (val >> 8) & 0xFF
# Mark last entry
last_cluster = clusters
off = fat_start * BYTES_PER_SECTOR + (last_cluster * 3 // 2)
if last_cluster % 2 == 0:
    val = (0xFFF & 0xFFF) | (image[off] << 8)
    image[off] = val & 0xFF
    image[off+1] = (val >> 8) & 0xFF
else:
    val = image[off] | ((0xFFF & 0xFFF) << 4)
    image[off] = val & 0xFF
    image[off+1] = (val >> 8) & 0xFF

# Root directory entry for KERNEL.BIN
entry_offset = root_start * BYTES_PER_SECTOR
# Filename (11 bytes, space-padded)
image[entry_offset:entry_offset+11] = b"KERNEL  BIN"
image[entry_offset+11] = 0x20  # attributes: archive
image[entry_offset+12:entry_offset+22] = bytes(10)  # reserved
image[entry_offset+22:entry_offset+24] = struct.pack("<H", 0)  # create time
image[entry_offset+24:entry_offset+26] = struct.pack("<H", 0)  # create date
image[entry_offset+26:entry_offset+28] = struct.pack("<H", 2)  # first cluster
image[entry_offset+28:entry_offset+32] = struct.pack("<I", kernel_size)  # file size

# Copy kernel data to data area
data_off = data_start * BYTES_PER_SECTOR
image[data_off:data_off + kernel_size] = kernel_data

# Write image
with open(OUTPUT, "wb") as f:
    f.write(image)

kernel_sectors_calc = (kernel_size + 511) // 512
print(f"Kernel: {kernel_size} bytes ({kernel_sectors_calc} sectors)")
print(f"Image: {len(image)} bytes ({len(image)//512} sectors)")
print(f"Layout: FAT={fat_start}-{fat_start+FAT_SECTORS-1} "
      f"ROOT={root_start}-{root_start+ROOT_SECTORS-1} "
      f"DATA={data_start}+")
print(f"Bootloader: boot.bin ({os.path.getsize(BOOT_SRC)} bytes)")
print(f"Image created: {OUTPUT}")
