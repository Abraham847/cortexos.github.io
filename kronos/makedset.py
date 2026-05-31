"""Create XOR dataset binary file"""
import struct

# XOR data: 4 samples, 2 inputs, 1 output
data = [
    (0.0, 0.0, 0.0),
    (0.0, 1.0, 1.0),
    (1.0, 0.0, 1.0),
    (1.0, 1.0, 0.0),
]

FPS = 16
F1 = 1 << FPS

with open("xordset.bin", "wb") as f:
    # header: num_samples, num_inputs, num_outputs
    f.write(struct.pack('<iii', len(data), 2, 1))
    for x1, x2, y in data:
        f.write(struct.pack('<ii', int(x1 * F1), int(x2 * F1)))
        f.write(struct.pack('<i', int(y * F1)))
print(f"Created xordset.bin ({len(data)} samples)")
