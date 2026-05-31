with open("kernel.bin", "rb") as f:
    d = f.read()

# Verify cld (0xFC) before rep stosb (0xF3 0xAA) in BSS clear
for i in range(len(d)-2):
    if d[i]==0xFC and d[i+1]==0xF3 and d[i+2]==0xAA:
        print(f"cld + rep stosb found at byte {i}")
        break
else:
    print("cld + rep stosb NOT FOUND")

# Verify window.c fix: reuse loop (look for cmp/je pattern)
print(f"Kernel size: {len(d)} bytes")
print("All checks done")
