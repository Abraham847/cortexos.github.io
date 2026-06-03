; x86-64 Long Mode Entry Point
; Loaded by bootloader at physical 0x10000 (0x1000:0)
; Starts in 16-bit real mode, transitions to 64-bit long mode

[bits 16]
section .text

KERNEL_PHY     equ 0x10000   ; where we're loaded
PAGE_TABLE_BASE equ 0x2000   ; page tables at 8KB (safe, below kernel)

global _start
_start:
    ; We're in real mode at 0x1000:0 (physical 0x10000)
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7000

    ; Save boot drive
    mov [boot_drive], dl

    mov si, msg_entry
    call print_16

    ; Enable A20 gate
    call enable_a20

    ; Load GDT for protected mode
    lgdt [gdtr32]

    ; Enable protected mode
    mov eax, cr0
    or al, 1
    mov cr0, eax

    ; Far jump to 32-bit code
    jmp 0x08:prot_mode_32

[bits 32]
prot_mode_32:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov esp, 0x70000

    mov si, msg_pm32
    call print_32

    ; Check for CPUID
    pushfd
    pop eax
    mov ecx, eax
    xor eax, 1 << 21
    push eax
    popfd
    pushfd
    pop eax
    cmp eax, ecx
    je no_cpuid

    ; Check for long mode
    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb no_long_mode

    mov eax, 0x80000001
    cpuid
    test edx, 1 << 29
    jz no_long_mode

    ; Set up page tables for long mode
    call setup_page_tables

    ; Enable PAE
    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax

    ; Load PML4 address
    mov eax, PAGE_TABLE_BASE
    mov cr3, eax

    ; Enable long mode in EFER
    mov ecx, 0xC0000080    ; EFER MSR
    rdmsr
    or eax, 1 << 8         ; LME bit
    wrmsr

    ; Enable paging
    mov eax, cr0
    or eax, 1 << 31        ; PG bit
    mov cr0, eax

    ; Load 64-bit GDT
    lgdt [gdtr64]

    ; Far jump to 64-bit code
    jmp 0x08:long_mode_64

no_cpuid:
    mov si, msg_no_cpuid
    call print_32
    hlt
    jmp $

no_long_mode:
    mov si, msg_no_lm
    call print_32
    hlt
    jmp $

[bits 64]
long_mode_64:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov rsp, 0x70000

    ; Clear screen using VGA text mode
    mov rdi, 0xB8000
    mov rcx, 2000
    mov rax, 0x0720072007200720
    rep stosq

    ; Print welcome
    mov rdi, 0xB8000
    mov rsi, msg_welcome
    call print_64

    ; Jump to kernel main
    jmp kernel_main

; ============================================================
; FUNCTIONS
; ============================================================

[bits 16]
enable_a20:
    push ax
    mov al, 0xDD
    out 0x64, al
    pop ax
    ret

print_16:
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

[bits 32]
print_32:
    push eax
    push edx
    push esi
    mov edx, 0xB8000
.next:
    lodsb
    test al, al
    jz .done
    mov ah, 0x07
    mov [edx], ax
    add edx, 2
    jmp .next
.done:
    pop esi
    pop edx
    pop eax
    ret

[bits 64]
print_64:
    push rax
    push rdx
    push rsi
    mov rdx, rdi
.next:
    lodsb
    test al, al
    jz .done
    mov ah, 0x07
    mov [rdx], ax
    add rdx, 2
    jmp .next
.done:
    pop rsi
    pop rdx
    pop rax
    ret

[bits 32]
setup_page_tables:
    push eax
    push ecx
    push edi

    mov edi, PAGE_TABLE_BASE
    xor eax, eax
    mov ecx, 4096 * 4      ; 4 page tables (PML4 + PDPT + PD + PT1)
    rep stosb

    ; PML4 entry 0 -> PDPT at PAGE_TABLE_BASE + 0x1000
    mov edi, PAGE_TABLE_BASE
    lea eax, [edi + 0x1000]
    or eax, 3               ; present + writable
    mov [edi], eax

    ; PML4 entry 511 (higher half) -> same PDPT
    lea eax, [edi + 0x1000]
    or eax, 3
    mov [edi + 511*8], eax

    ; PDPT entry 0 -> PD at PAGE_TABLE_BASE + 0x2000
    lea edi, [edi + 0x1000]
    lea eax, [edi + 0x1000]
    or eax, 3
    mov [edi], eax

    ; PD entry 0 -> PT at PAGE_TABLE_BASE + 0x3000
    lea edi, [edi + 0x1000]
    lea eax, [edi + 0x1000]
    or eax, 3
    mov [edi], eax

    ; PT: identity map first 2MB
    lea edi, [edi + 0x1000]
    xor eax, eax
    mov ecx, 512
.map_pt:
    mov [edi], eax
    or dword [edi], 3       ; present + writable
    add edi, 8
    add eax, 0x1000         ; next 4KB page
    loop .map_pt

    pop edi
    pop ecx
    pop eax
    ret

; ============================================================
; DATA
; ============================================================

boot_drive   db 0

msg_entry    db "[64] Entering protected mode...", 13, 10, 0
msg_pm32     db "[64] Setting up long mode...", 0
msg_no_cpuid db "ERROR: CPUID not supported", 0
msg_no_lm    db "ERROR: Long mode not supported", 0
msg_welcome  db "KronOS64 v0.1 - x86-64 Long Mode", 0

; GDT for 32-bit protected mode
align 8
gdt32:
    dq 0                     ; null
    dw 0xFFFF, 0, 0x9A00, 0x00CF  ; code (ring 0)
    dw 0xFFFF, 0, 0x9200, 0x00CF  ; data (ring 0)
gdt32_end:

gdtr32:
    dw gdt32_end - gdt32 - 1
    dd gdt32

; GDT for 64-bit long mode
align 8
gdt64:
    dq 0                     ; null
    dq 0x00209A0000000000    ; 64-bit code segment
    dq 0x0000920000000000    ; data segment
gdt64_end:

gdtr64:
    dw gdt64_end - gdt64 - 1
    dd gdt64
    dd 0                     ; upper 32 bits of address (unused in 32-bit)
