import struct, os

with open("cortexos.img", "rb") as f:
    img = f.read()

rd = img[19*512:19*512+224*32]
for i in range(224):
    e = rd[i*32:(i+1)*32]
    if e[0] == 0:
        break
    if e[0] == 0xE5:
        continue
    name = e[0:11].decode("ascii", errors="replace")
    sz = struct.unpack("<I", e[28:32])[0]
    print(f"{name} size={sz}")

print(f"kernel.bin exists: {os.path.exists('kernel.bin')}")
print(f"xordset.bin exists: {os.path.exists('xordset.bin')}")
