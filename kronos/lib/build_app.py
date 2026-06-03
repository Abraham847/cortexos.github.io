#!/usr/bin/env python3
"""Build a CortexOS app from a C source file."""
import struct, sys, os

def build(source, output):
    src_name = os.path.splitext(os.path.basename(source))[0]
    cc = os.environ.get('CC', '~/gcc-cross/bin/i686-linux-musl-gcc')
    inc = '-Ilib/libc -Ikernel -Ikernel/core -Ikernel/drivers -Ikernel/ui'
    flags = f'-ffreestanding -nostdlib -m32 -fno-pic -mno-red-zone {inc}'

    # Compile crt0, libc, and the app
    os.system(f'{cc} {flags} -c lib/crt0.c -o /tmp/crt0.o')
    os.system(f'{cc} {flags} -c lib/libc.c -o /tmp/libc.o')
    os.system(f'{cc} {flags} -c {source} -o /tmp/app.o')

    # Link as flat binary
    os.system(f'{cc} -ffreestanding -nostdlib -m32 -no-pie '
              f'-Wl,--oformat,binary -Wl,-Ttext,0x1000 '
              f'-o /tmp/raw.bin /tmp/crt0.o /tmp/libc.o /tmp/app.o')

    # Read raw binary
    with open('/tmp/raw.bin', 'rb') as f:
        data = f.read()

    # Ensure first bytes are 0x55 0x89 (push ebp; mov ...,...)
    if data[0] != 0x55 or data[1] != 0x89:
        print(f'WARNING: First bytes are {data[0]:02x} {data[1]:02x}, not 55 89')

    # Calculate checksum
    csum = 0
    for b in data:
        csum ^= b

    # Write output with magic + data + checksum
    with open(output, 'wb') as f:
        f.write(data)
        f.write(struct.pack('B', csum))

    size = len(data) + 1
    print(f'Built {output}: {size} bytes')

if __name__ == '__main__':
    if len(sys.argv) < 3:
        print(f'Usage: {sys.argv[0]} source.c output.bin')
        sys.exit(1)
    build(sys.argv[1], sys.argv[2])
