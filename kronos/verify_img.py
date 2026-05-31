import struct

with open("cortexos.img", "rb") as f:
    boot = f.read(512)
    lba = struct.unpack("<I", boot[506:510])[0]
    bps = struct.unpack("<H", boot[11:13])[0]
    spc = boot[13]
    fat_count = boot[16]
    spf = struct.unpack("<H", boot[22:24])[0]
    root_entries = struct.unpack("<H", boot[17:19])[0]
    reserved = struct.unpack("<H", boot[14:16])[0]

    root_sectors = (root_entries * 32 + bps - 1) // bps
    data_start = reserved + fat_count * spf + root_sectors

    print(f"BPB OK, data_start={data_start}, kernel_lba={lba}")
    print(f"Data matches: {data_start == lba}")

    f.seek(data_start * bps)
    img_data = f.read(23844)

with open("kernel.bin", "rb") as f:
    file_data = f.read(23844)

print(f"Kernel size matches: {len(img_data) == len(file_data)}")
print(f"Kernel content matches: {img_data == file_data}")
print("READY" if (data_start == lba and img_data == file_data) else "FAIL")
