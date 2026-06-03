; FAT12 Bootloader - loads KERNEL.BIN to 0x7E00, jumps to it
; NASM -f bin
[org 0x7C00]
[bits 16]

KERNEL_ADDR equ 0x7E00
ROOT_SECTORS equ 14
FAT_SECTORS  equ 9

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    mov [boot_drive], dl

    mov si, msg_boot
    call print

    ; Load root directory to 0x7E00
    mov ax, 19           ; root dir starts at sector 19
    mov cx, ROOT_SECTORS
    mov bx, 0x7E00
    call read_sectors

    ; Search for KERNELBIN in root dir
    mov cx, 224
    mov di, 0x7E00
.search_loop:
    push cx
    mov si, kernel_name
    mov cx, 11
    push di
    rep cmpsb
    pop di
    je .found
    pop cx
    add di, 32
    loop .search_loop

    mov si, msg_not_found
    call print
    jmp hang

.found:
    pop cx
    mov ax, [di + 26]    ; first cluster
    mov [cluster], ax

    ; Load FAT to 0x7E00 (overwrites root dir, that's fine)
    mov ax, 1
    mov cx, FAT_SECTORS
    mov bx, 0x7E00
    call read_sectors

    ; Load kernel data to 0x1000:0 (segmented)
    mov ax, 0x1000
    mov es, ax
    xor bx, bx
    mov dx, 31           ; data area starts at sector 31 (0-based: 33)
    ; Actually data area starts at sector 33 (0-based counting from 0)
    ; sector 1-9: FAT, 19-32: root dir, 33+: data
    mov dx, 33

.load_loop:
    mov ax, [cluster]
    cmp ax, 0xFF8
    jae .done

    ; Convert cluster to sector
    ; sector = data_start + (cluster - 2) * sectors_per_cluster
    sub ax, 2
    mov si, ax
    xor ah, ah
    mov cl, 1            ; sectors per cluster
    mul cl
    add ax, 33

    push dx
    mov cx, 1
    call read_sectors_seg
    pop dx

    ; Advance buffer by 512 bytes
    mov ax, es
    add ax, 0x20
    mov es, ax

    ; Get next cluster from FAT
    mov ax, [cluster]
    mov cx, 3
    mul cx
    shr ax, 1
    mov si, ax
    mov ax, [0x7E00 + si]
    mov dx, [cluster]
    test dx, 1
    jz .even
    shr ax, 4
    jmp .next
.even:
    and ax, 0x0FFF
.next:
    mov [cluster], ax
    jmp .load_loop

.done:
    mov si, msg_ok
    call print

    ; Jump to kernel
    mov dl, [boot_drive]
    mov ax, 0x1000
    mov ds, ax
    mov es, ax
    jmp 0x1000:0000

hang:
    hlt
    jmp hang

; Read sectors to seg:bx
read_sectors_seg:
    push ax
    push cx
    push dx
    push es
    mov ah, 02h
    mov al, cl
    mov ch, 0
    mov cl, al
    xchg ch, cl
    mov dh, 0
    mov dl, [boot_drive]
    int 13h
    pop es
    pop dx
    pop cx
    pop ax
    ret

; Read sectors to es:bx
read_sectors:
    push ax
    push cx
    push dx
    mov ah, 02h
    mov al, cl
    mov ch, 0
    mov cl, al
    xchg ch, cl
    mov dh, 0
    mov dl, [boot_drive]
    int 13h
    pop dx
    pop cx
    pop ax
    ret

print:
    push ax
    push bx
.next:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    xor bx, bx
    int 10h
    jmp .next
.done:
    pop bx
    pop ax
    ret

msg_boot      db "[KRONOS64] Loading...", 13, 10, 0
msg_not_found db "KERNEL.BIN not found!", 13, 10, 0
msg_ok        db "OK", 13, 10, 0
kernel_name   db "KERNEL  BIN"
boot_drive    db 0
cluster       dw 0

times 510-($-$$) db 0
dw 0xAA55
