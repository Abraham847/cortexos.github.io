import os, struct, sys

def build(kernel_bin_path, boot_bin_path, img_path, extra_files=None):
    BPS = 512
    SPC = 1
    RESERVED = 1
    FAT_COUNT = 2
    ROOT_ENTRIES = 224
    TOTAL_SECTORS = 2880
    SECTORS_PER_FAT = 9
    MEDIA = 0xF0

    ROOT_DIR_SECTORS = (ROOT_ENTRIES * 32 + BPS - 1) // BPS
    DATA_START = RESERVED + FAT_COUNT * SECTORS_PER_FAT + ROOT_DIR_SECTORS

    with open(kernel_bin_path, 'rb') as f:
        kernel_data = f.read()
    kernel_sectors = (len(kernel_data) + BPS - 1) // BPS

    with open(boot_bin_path, 'rb') as f:
        boot_data = bytearray(f.read())
    if len(boot_data) != BPS:
        boot_data = boot_data.ljust(BPS, b'\x00')[:BPS]

    struct.pack_into('<I', boot_data, 506, DATA_START)
    struct.pack_into('<H', boot_data, 11, BPS)
    struct.pack_into('B',  boot_data, 13, SPC)
    struct.pack_into('<H', boot_data, 14, RESERVED)
    struct.pack_into('B',  boot_data, 16, FAT_COUNT)
    struct.pack_into('<H', boot_data, 17, ROOT_ENTRIES)
    struct.pack_into('<H', boot_data, 19, TOTAL_SECTORS)
    struct.pack_into('B',  boot_data, 21, MEDIA)
    struct.pack_into('<H', boot_data, 22, SECTORS_PER_FAT)
    struct.pack_into('<H', boot_data, 24, 18)
    struct.pack_into('<H', boot_data, 26, 2)

    img = bytearray(BPS * TOTAL_SECTORS)
    img[0:BPS] = boot_data

    fat_size = SECTORS_PER_FAT * BPS
    fat = bytearray(fat_size)
    fat[0] = MEDIA; fat[1] = 0xFF; fat[2] = 0xFF; fat[3] = 0xFF

    # Build list of files to add: (name_83_bytes, data_bytes)
    files = [(b'KERNEL  BIN', kernel_data)]
    if extra_files:
        for fname, fpath in extra_files:
            with open(fpath, 'rb') as f:
                fdata = f.read()
            parts = fname.upper().split('.')
            base = parts[0].ljust(8)[:8].encode('ascii')
            ext = (parts[1].ljust(3)[:3] if len(parts) > 1 else '   ').encode('ascii')
            name83 = base + ext
            files.append((name83, fdata))

    cluster = 2
    for name83, fdata in files:
        sectors = (len(fdata) + BPS - 1) // BPS
        for i in range(sectors):
            next_cl = cluster + 1 if i < sectors - 1 else 0xFFF
            off = cluster * 3 // 2
            if cluster % 2 == 0:
                fat[off] = next_cl & 0xFF
                fat[off + 1] = (fat[off + 1] & 0xF0) | ((next_cl >> 8) & 0x0F)
            else:
                fat[off] = (fat[off] & 0x0F) | ((next_cl & 0x0F) << 4)
                fat[off + 1] = (next_cl >> 4) & 0xFF
            data_off = (DATA_START + cluster - 2) * BPS
            chunk = fdata[i * BPS:(i + 1) * BPS]
            img[data_off:data_off + len(chunk)] = chunk
            cluster += 1
        # Write root dir entry
        rd_off = (RESERVED + FAT_COUNT * SECTORS_PER_FAT) * BPS
        ent_off = None
        for ent_idx in range(ROOT_ENTRIES):
            off = rd_off + ent_idx * 32
            if img[off] == 0 or img[off] == 0xE5:
                ent_off = off
                break
        if ent_off is None:
            print("ERROR: root dir full")
            sys.exit(1)
        entry = bytearray(32)
        for i in range(11):
            entry[i] = name83[i]
        fcl = cluster - sectors
        struct.pack_into('<H', entry, 26, fcl)
        struct.pack_into('<I', entry, 28, len(fdata))
        img[ent_off:ent_off + 32] = entry

    f1_off = RESERVED * BPS
    img[f1_off:f1_off + fat_size] = fat
    f2_off = (RESERVED + SECTORS_PER_FAT) * BPS
    img[f2_off:f2_off + fat_size] = fat

    with open(img_path, 'wb') as f:
        f.write(img)

    total_bytes = sum(len(d) for _, d in files)
    print(f"Image: {img_path}")
    print(f"Files: {len(files)} ({total_bytes}B total)")
    print(f"Size: {len(img)}B ({TOTAL_SECTORS} sectors)")
    print(f"Kernel at LBA {DATA_START}")

if __name__ == '__main__':
    import sys
    krnl = sys.argv[1] if len(sys.argv) > 1 else 'kernel.bin'
    boot = sys.argv[2] if len(sys.argv) > 2 else 'boot.bin'
    img  = sys.argv[3] if len(sys.argv) > 3 else 'cortexos.img'
    extra = []
    for arg in sys.argv[4:]:
        if '=' in arg:
            parts = arg.split('=', 1)
            extra.append((parts[0], parts[1]))
        else:
            base = os.path.basename(arg)
            extra.append((base, arg))
    build(krnl, boot, img, extra)
