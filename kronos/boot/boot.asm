; FAT12 bootloader for CortexOS
; Supports QEMU (extended INT 13h) and VirtualBox (CHS fallback)
[org 0x7C00]
[bits 16]

; --- BIOS Parameter Block ---
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
    db 0              ; drive number (BIOS sets this)
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

    ; Try extended read (INT 13h AH=42h)
    mov di, 0x0600
    mov byte [di], 0x10
    mov byte [di+1], 0
    mov word [di+2], 128
    mov word [di+4], 0x7E00
    mov word [di+6], 0
    mov ax, [kernel_lba]
    mov [di+8], ax
    mov ax, [kernel_lba+2]
    mov [di+10], ax
    mov word [di+12], 0
    mov word [di+14], 0

    mov ah, 0x42
    mov si, 0x0600
    mov dl, [boot_drive]
    int 0x13
    jnc loaded

    ; CHS fallback (for VirtualBox floppy, old BIOS, etc.)
    mov ax, [kernel_lba]
    mov dx, [kernel_lba+2]
    mov cx, 128
    mov di, 0x7E00

.chs:
    mov [lba_lo], ax
    mov [lba_hi], dx
    push cx
    push di

    ; LBA -> CHS: sector = (LBA % 18) + 1, track = LBA / 18
    mov cx, 18
    div cx
    inc dx
    mov [sec], dl

    ; track -> CHS: cylinder = track / 2, head = track % 2
    xor dx, dx
    mov cx, 2
    div cx
    mov ch, al
    mov cl, [sec]
    mov dh, dl

    pop bx
    push bx
    mov ah, 0x02
    mov al, 1
    mov dl, [boot_drive]
    int 0x13
    jc disk_error

    pop di
    pop cx
    add di, 512
    mov ax, [lba_lo]
    mov dx, [lba_hi]
    add ax, 1
    adc dx, 0
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

msg_loading db 'CortexOS v1.0', 0x0D, 0x0A, 'Loading...', 0
msg_err db 'ERR', 0
boot_drive db 0
lba_lo dw 0
lba_hi dw 0
sec db 0

times 506-($-$$) db 0
kernel_lba dd 1      ; Patched by build script (LBA of first kernel sector)
dw 0xAA55
