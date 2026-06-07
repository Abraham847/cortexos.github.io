section .text.start
[bits 16]
global start
extern kmain
global boot_drive

MEM_MAP_ADDR  equ 0x1000
MEM_INFO_ADDR equ 0x2000
VBE_INFO_ADDR equ 0x2100

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    mov [boot_drive], dl

    mov ax, 0x0013
    int 0x10
    call detect_memory
    call get_extended_mem
    call enable_a20
    call detect_vbe

    lgdt [gdt_desc]

    mov eax, cr0
    or eax, 1
    mov cr0, eax

    jmp 0x08:pmode_init

detect_memory:
    mov di, MEM_MAP_ADDR
    xor ebx, ebx
    xor bp, bp
    mov edx, 0x534D4150
    mov eax, 0xE820
    mov ecx, 24
    int 0x15
    jc .done
    mov edx, 0x534D4150
    cmp eax, edx
    jne .done
    inc bp
    test ebx, ebx
    je .done
.loop:
    add di, 24
    inc bp
    mov eax, 0xE820
    mov ecx, 24
    int 0x15
    jc .done
    mov edx, 0x534D4150
    cmp eax, edx
    jne .done
    test ebx, ebx
    jne .loop
.done:
    mov [MEM_INFO_ADDR], bp
    ret

get_extended_mem:
    mov ax, 0xE801
    int 0x15
    jc .alt
    mov [MEM_INFO_ADDR + 4], ax
    mov [MEM_INFO_ADDR + 6], bx
    ret
.alt:
    mov ah, 0x88
    int 0x15
    jc .none
    mov [MEM_INFO_ADDR + 4], ax
    mov word [MEM_INFO_ADDR + 6], 0
    ret
.none:
    mov word [MEM_INFO_ADDR + 4], 0
    mov word [MEM_INFO_ADDR + 6], 0
    ret

enable_a20:
    mov ax, 0x2401
    int 0x15
    ret

detect_vbe:
    pusha
    mov word [.mode_idx], 0
.try_next:
    mov si, .mode_table
    xor ax, ax
    mov al, [.mode_idx]
    add si, ax
    mov cx, [si]
    cmp cx, 0xFFFF
    je .fallback

    mov ax, 0x4F01
    mov di, 0x5100
    int 0x10
    cmp ax, 0x004F
    jne .next_mode

    test byte [0x5100], 0x80
    jz .next_mode

    mov bx, cx
    or bx, 0x4000
    mov ax, 0x4F02
    int 0x10
    cmp ax, 0x004F
    jne .next_mode

    mov ax, [0x5112]
    mov [VBE_INFO_ADDR], ax
    mov ax, [0x5114]
    mov [VBE_INFO_ADDR + 2], ax
    mov al, [0x5119]
    mov [VBE_INFO_ADDR + 6], al
    mov ax, [0x5110]
    mov [VBE_INFO_ADDR + 12], ax
    mov eax, [0x5128]
    mov [VBE_INFO_ADDR + 8], eax
    mov byte [VBE_INFO_ADDR + 7], 1
    popa
    ret
.next_mode:
    inc byte [.mode_idx]
    jmp .try_next
.fallback:
    mov byte [VBE_INFO_ADDR + 7], 0
    popa
    ret

.mode_table:
    dw 0x0105, 0x0103, 0x0101, 0xFFFF
.mode_idx: db 0

gdt_start:   dq 0
gdt_code:    dw 0xFFFF, 0
             db 0, 0x9A, 0xCF, 0
gdt_data:    dw 0xFFFF, 0
             db 0, 0x92, 0xCF, 0
gdt_end:
gdt_desc:    dw gdt_end - gdt_start - 1
             dd gdt_start

extern __bss_start, __bss_end

[bits 32]
pmode_init:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x100000

    call detect_cpu

    ; Enable FPU
    mov eax, cr0
    and eax, 0xFFFFFFFB
    or eax, 0x22
    mov cr0, eax
    fninit

    cld
    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    xor al, al
    rep stosb

    call kmain
    cli
    hlt

detect_cpu:
    pushfd
    pop eax
    mov ecx, eax
    xor eax, 0x200000
    push eax
    popfd
    pushfd
    pop eax
    xor eax, ecx
    push ecx
    popfd
    test eax, 0x200000
    jz .done
    mov eax, 0
    cpuid
    mov [MEM_INFO_ADDR + 16], ebx
    mov [MEM_INFO_ADDR + 20], edx
    mov [MEM_INFO_ADDR + 24], ecx
    mov byte [MEM_INFO_ADDR + 8], 1
    ret
.done:
    mov byte [MEM_INFO_ADDR + 8], 0
    ret

boot_drive: db 0
