import struct

with open("cortexos.img", "rb") as f:
    img = f.read()

fat = img[512:512+4608]

def get(cl):
    off = cl * 3 // 2
    if off + 1 >= len(fat):
        return 0xFFF
    if cl % 2 == 0:
        return fat[off] | ((fat[off+1] & 0x0F) << 8)
    else:
        return ((fat[off] >> 4) & 0x0F) | (fat[off+1] << 4)

# Read root directory (sectors 19-32)
rd = img[19 * 512:19 * 512 + 224 * 32]

for i in range(224):
    e = rd[i*32:(i+1)*32]
    if e[0] == 0:
        break
    if e[0] == 0xE5:
        continue
    name = e[0:11].decode("ascii", errors="replace")
    cl = struct.unpack("<H", e[26:28])[0]
    sz = struct.unpack("<I", e[28:32])[0]
    
    chain = []
    while cl >= 2 and cl < 0xFF8 and len(chain) < 200:
        chain.append(cl)
        ncl = get(cl)
        if ncl == cl:
            break
        cl = ncl
    
    expected = (sz + 511) // 512 if sz > 0 else 0
    ok = len(chain) == expected or (sz == 0 and len(chain) == 0)
    print(f"{name} start={chain[0] if chain else -1} sectors={len(chain)} size={sz} expected={expected} {'OK' if ok else 'CORRUPTED'}")
    
    # Verify chain integrity
    for j, c in enumerate(chain):
        nc = get(c)
        if j < len(chain) - 1 and nc != chain[j+1]:
            print(f"  BAD: cluster {c} -> {nc}, expected {chain[j+1]}")
        if j == len(chain) - 1 and nc != 0xFFF:
            print(f"  BAD: last cluster {c} -> {nc}, expected 0xFFF")
