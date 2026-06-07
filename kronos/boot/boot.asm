; CortexOS bootloader: floppy + HDD with partition table
[org 0x7C00]
[bits 16]

; --- BIOS Parameter Block (FAT12 floppy compatible) ---
    jmp short start
    nop
    db 'CORTEXOS'     ; OEM (8)
    dw 512            ; bytes/sector
    db 1              ; sectors/cluster
    dw 1              ; reserved sectors
    db 2              ; FAT count
    dw 224            ; root entries
    dw 2880           ; total sectors
    db 0xF0           ; media descriptor
    dw 9              ; sectors/FAT
    dw 18             ; sectors/track
    dw 2              ; heads
    dd 0              ; hidden sectors
    dd 0              ; large sectors

; Extended BPB
    db 0              ; drive number
    db 0              ; reserved
    db 0x29           ; signature
    dd 0x20250530     ; volume serial
    db 'CORTEXOS   '  ; volume label (11)
    db 'FAT12   '     ; filesystem type (8)

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    mov [boot_drive], dl
    sti

    mov si, msg_loading
    call print

    ; Read from LBA base (partition start or absolute)
    mov eax, [kernel_base_lba]

    ; Try extended read (INT 13h AH=42h)
    mov di, 0x0600
    mov byte [di], 0x10
    mov byte [di+1], 0

    ; Calculate kernel size in sectors
    mov ebx, [kernel_size_bytes]
    add ebx, 511
    shr ebx, 9
    mov word [di+2], bx    ; sectors to read

    mov word [di+4], 0x7E00    ; buffer
    mov word [di+6], 0
    mov [di+8], eax
    mov word [di+12], 0
    mov word [di+14], 0

    mov ah, 0x42
    mov si, 0x0600
    mov dl, [boot_drive]
    int 0x13
    jnc loaded

    ; CHS fallback
    mov eax, [kernel_base_lba]
    mov cx, 400
    mov di, 0x7E00

.chs:
    mov [lba_val], eax
    push cx
    push di

    mov ebx, eax
    xor edx, edx
    movzx ecx, byte [bp_sec_per_track]
    div ecx
    inc dx
    mov [sec], dl

    xor edx, edx
    movzx ecx, byte [bp_heads]
    div ecx
    mov ch, al
    mov cl, [sec]
    mov dh, dl

    mov bx, di
    mov ah, 0x02
    mov al, 1
    mov dl, [boot_drive]
    int 0x13
    jc disk_error

    pop di
    pop cx
    add di, 512
    mov eax, [lba_val]
    inc eax
    dec cx
    jnz .chs

loaded:
    mov dl, [boot_drive]
    jmp 0x7E00

disk_error:
    mov si, msg_err
    call print
    hlt
    jmp $

print:
    lodsb
    or al, al
    jz .done
    mov ah, 0x0E
    int 0x10
    jmp print
.done:
    ret

msg_loading db 'CortexOS', 0x0D, 0x0A, 0
msg_err db 'ERR', 0
boot_drive db 0
sec db 0
lba_val dd 0
bp_sec_per_track dw 18
bp_heads dw 2

times 438-($-$$) db 0

; Kernel location (4 bytes each, at offsets 438 and 442)
kernel_base_lba:
    dd 1                ; Start LBA of kernel
kernel_size_bytes:
    dd 0                ; Kernel size in bytes (patched at build time)

; Partition table at offset 446 (0x1BE)
partition_table:
times 64 db 0

dw 0xAA55
