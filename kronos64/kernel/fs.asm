; KronOS64 - FAT12 Filesystem Driver + ATA PIO
; Uses primary ATA channel (IRQ14 not used, PIO polling)
; 64-bit long mode

; Disk buffer (512 bytes per sector)
disk_buf: times 512 db 0
fat_buf:  times 512*9 db 0   ; enough for full FAT (9 sectors)
root_buf: times 512*14 db 0  ; enough for root dir (14 sectors)

; ATA PIO - Read one sector via primary channel
; in: rax = LBA sector number (28-bit)
; out: buffer at disk_buf filled
; clobbers: rax, rcx, rdx, rdi
ata_read_sector:
    push rax
    push rcx
    push rdx
    push rdi

    mov rcx, rax           ; save LBA in rcx

    ; Wait for drive ready
    mov rdx, 0x1F7
.wait_bsy:
    in al, dx
    test al, 0x80
    jnz .wait_bsy
.wait_rdy:
    in al, dx
    test al, 0x40
    jz .wait_rdy

    ; Sector count = 1
    mov rdx, 0x1F2
    mov al, 1
    out dx, al

    ; LBA bits 0-7
    mov rax, rcx
    mov rdx, 0x1F3
    out dx, al

    ; LBA bits 8-15
    mov rax, rcx
    shr rax, 8
    mov rdx, 0x1F4
    out dx, al

    ; LBA bits 16-23
    mov rax, rcx
    shr rax, 16
    mov rdx, 0x1F5
    out dx, al

    ; LBA bits 24-27 + drive select
    mov rax, rcx
    shr rax, 24
    and al, 0x0F
    or al, 0xE0            ; LBA mode, master
    mov rdx, 0x1F6
    out dx, al

    ; Send read command
    mov rdx, 0x1F7
    mov al, 0x20
    out dx, al

    ; Wait for data ready
.poll:
    in al, dx
    test al, 0x80
    jnz .poll
    test al, 0x08
    jz .poll
    test al, 0x21
    jnz .error

    ; Read 256 words (512 bytes) to disk_buf
    mov rdx, 0x1F0
    mov rdi, disk_buf
    mov rcx, 256
    rep insw

    pop rdi
    pop rdx
    pop rcx
    pop rax
    ret

.error:
    mov rdi, [vga_cursor]
    add rdi, 0xB8000
    mov rsi, msg_ata_err
    call vga_puts
    pop rdi
    pop rdx
    pop rcx
    pop rax
    ret

; Initialize filesystem: read FAT and root directory
; Clobbers everything
fs_init:
    push rax
    push rcx
    push rdi

    ; Read FAT (sectors 1-9) into fat_buf
    mov rdi, fat_buf
    mov rax, 1
    mov rcx, 9
.next_fat:
    call ata_read_sector
    ; Copy from disk_buf to current position in fat_buf
    push rcx
    push rsi
    mov rsi, disk_buf
    mov rcx, 128           ; 512/4
    rep movsd
    pop rsi
    pop rcx
    inc rax
    loop .next_fat

    ; Read root directory (sectors 19-32 = 14 sectors) into root_buf
    mov rdi, root_buf
    mov rax, 19
    mov rcx, 14
.next_root:
    call ata_read_sector
    push rcx
    push rsi
    mov rsi, disk_buf
    mov rcx, 128
    rep movsd
    pop rsi
    pop rcx
    inc rax
    loop .next_root

    pop rdi
    pop rcx
    pop rax
    ret

; Find file in root directory
; in: rsi = filename (11 bytes, space-padded, uppercase)
; out: rdi = root entry (32 bytes), or 0 if not found
; clobbers: rax, rcx, rsi, rdi
fs_find:
    push rbx
    mov rdi, root_buf
    mov rcx, 224           ; max entries
.next:
    push rcx
    push rdi
    push rsi
    mov rcx, 11
    rep cmpsb
    pop rsi
    pop rdi
    je .found
    pop rcx
    add rdi, 32            ; next entry
    loop .next
    xor rdi, rdi           ; not found
    pop rbx
    ret
.found:
    pop rcx
    pop rbx
    ret

; Read file into buffer
; in: rsi = filename (11 bytes), rdi = destination buffer, rdx = max size
; out: rax = bytes read, or -1 on error
; clobbers: everything
fs_read_file:
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi

    call fs_find
    test rdi, rdi
    jz .not_found

    ; Get file size from directory entry
    mov eax, [rdi + 28]    ; file size
    mov [file_size], eax
    cmp eax, edx
    jbe .size_ok
    mov eax, edx           ; limit to buffer size
.size_ok:
    mov [bytes_to_read], eax

    ; Get first cluster
    movzx eax, word [rdi + 26]
    mov [cluster], ax

    ; Read all clusters
    mov rdi, [rsp]         ; original destination buffer from stack
.read_loop:
    mov ax, [cluster]
    cmp ax, 0xFF8          ; EOC marker
    jae .done

    ; Convert cluster to LBA
    ; data_start = 33, cluster 2 -> sector 33
    sub ax, 2
    add ax, 33
    movzx rax, ax
    call ata_read_sector

    ; Copy 512 bytes to destination
    push rcx
    push rsi
    mov rsi, disk_buf
    mov rcx, 128           ; 512/4 = 128 dwords
    rep movsd
    pop rsi
    pop rcx

    ; Check if we've read enough
    sub qword [bytes_to_read], 512
    jbe .done

    ; Get next cluster from FAT
    movzx eax, word [cluster]
    mov ecx, 3
    mul ecx
    shr eax, 1             ; FAT entry offset = cluster * 3 / 2
    mov esi, eax
    mov ax, [fat_buf + rsi]
    movzx ecx, word [cluster]
    test ecx, 1
    jz .even
    shr ax, 4
    jmp .next_cluster
.even:
    and ax, 0x0FFF
.next_cluster:
    mov [cluster], ax
    jmp .read_loop

.done:
    mov rax, [file_size]
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    ret

.not_found:
    mov rax, -1
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    ret

; List root directory
; Prints filename (11 chars) and size
fs_list:
    push rax
    push rcx
    push rdi
    push rsi

    mov rdi, root_buf
    mov rcx, 224
    mov byte [entry_count], 0

.next:
    ; Check if entry is used (first byte != 0 and != 0xE5)
    cmp byte [rdi], 0
    je .skip
    cmp byte [rdi], 0xE5
    je .skip

    ; Print filename (11 chars)
    push rcx
    push rdi
    mov rcx, 11
    mov rsi, rdi
    mov rdi, [vga_cursor]
    add rdi, 0xB8000
.print_name:
    lodsb
    stosb
    inc rdi                ; skip attribute byte
    loop .print_name
    sub rdi, 0xB8000
    mov [vga_cursor], rdi

    ; Print size
    pop rdi
    mov eax, [rdi + 28]
    push rdi
    mov rdi, [vga_cursor]
    add rdi, 0xB8000
    ; Print space + size
    mov byte [rdi], ' '
    mov byte [rdi+1], 0x07
    add rdi, 2
    ; Convert size to string
    push rax
    push rbx
    push rcx
    push rdx
    mov rbx, 10
    xor rcx, rcx
.divloop:
    xor rdx, rdx
    div rbx
    push rdx
    inc rcx
    test rax, rax
    jnz .divloop
.printdigits:
    pop rax
    add al, '0'
    mov [rdi], al
    mov byte [rdi+1], 0x07
    add rdi, 2
    loop .printdigits
    pop rdx
    pop rcx
    pop rbx
    pop rax

    ; Newline
    xor al, al
    mov rcx, 80
    sub rax, [vga_cursor]
    xor rdx, rdx
    div rcx
    inc rax
    mov rcx, 160
    mul rcx
    mov [vga_cursor], rax

    pop rdi
    pop rcx
    inc byte [entry_count]

.skip:
    add rdi, 32
    dec rcx
    jnz .next

    pop rsi
    pop rdi
    pop rcx
    pop rax
    ret

msg_ata_err: db "ATA error!", 0

; Variables for FS
align 2
cluster:       dw 0
file_size:     dd 0
bytes_to_read: dq 0
entry_count:   db 0
