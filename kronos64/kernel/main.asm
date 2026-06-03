; KronOS64 - Kernel Main
; 64-bit long mode kernel - Unix-like minimal
; NASM -f bin (included via entry.asm)

[bits 64]
section .text

; These are defined within this file, not external
; isr_default, isr_kb, isr_timer are defined below

; Kernel main entry (called from entry.asm)
global kernel_main
kernel_main:
    ; Set up stack
    mov rsp, stack_top

    ; Initialize VGA text mode
    call vga_init

    ; Print banner
    mov rdi, vga_cursor
    mov rsi, banner
    call vga_puts

    ; Initialize IDT
    call idt_init

    ; Initialize keyboard
    call kb_init

    ; Initialize timer (PIT)
    call timer_init

    ; Initialize filesystem
    mov rsi, msg_fs_init
    call vga_puts
    call fs_init
    mov rsi, msg_ok
    call vga_puts
    call vga_newline

    ; Initialize shell
    call shell_start

    ; Should never return
    hlt
    jmp $

; ============================================================
; VGA Text Mode Driver
; ============================================================
; Output to 0xB8000 (80x25 VGA text mode)
; rdi = cursor position offset
; rsi = string to print

vga_cursor: dq 0           ; byte offset into VGA memory

vga_init:
    push rax
    push rcx
    push rdi
    mov rdi, 0xB8000
    mov rcx, 2000
    mov rax, 0x0720072007200720
    rep stosq
    mov qword [vga_cursor], 0
    pop rdi
    pop rcx
    pop rax
    ret

; rsi = string, destroys rsi, rdi preserved
vga_puts:
    push rax
    push rdi
    mov rdi, [vga_cursor]
    add rdi, 0xB8000
.next:
    lodsb
    test al, al
    jz .done
    cmp al, 10
    je .newline
    mov ah, 0x07
    stosw
    jmp .next
.newline:
    push rax
    mov rax, rdi
    sub rax, 0xB8000
    add rax, 160           ; next line
    mov rdi, 0xB8000
    xor rdx, rdx
    mov rcx, 160
    div rcx
    mul rcx
    add rdi, rax
    pop rax
    jmp .next
.done:
    sub rdi, 0xB8000
    mov [vga_cursor], rdi
    pop rdi
    pop rax
    ret

; Print hex value in rax
vga_puthex:
    push rax
    push rbx
    push rcx
    push rdi
    mov rdi, [vga_cursor]
    add rdi, 0xB8000
    mov rcx, 16
    mov rbx, rax
.next:
    rol rbx, 4
    mov al, bl
    and al, 0x0F
    cmp al, 10
    jb .digit
    add al, 'A' - 10
    jmp .store
.digit:
    add al, '0'
.store:
    mov ah, 0x07
    stosw
    loop .next
    sub rdi, 0xB8000
    mov [vga_cursor], rdi
    pop rdi
    pop rcx
    pop rbx
    pop rax
    ret

; Print char in al
vga_putchar:
    push rax
    push rdi
    mov rdi, [vga_cursor]
    add rdi, 0xB8000
    mov ah, 0x07
    stosw
    sub rdi, 0xB8000
    mov [vga_cursor], rdi
    pop rdi
    pop rax
    ret

; Newline
vga_newline:
    push rax
    push rdi
    mov rax, [vga_cursor]
    add rax, 160
    xor rdx, rdx
    mov rcx, 160
    div rcx
    mul rcx
    mov [vga_cursor], rax
    pop rdi
    pop rax
    ret

; Clear screen
vga_clear:
    push rax
    push rcx
    push rdi
    mov rdi, 0xB8000
    mov rcx, 2000
    mov rax, 0x0720072007200720
    rep stosq
    mov qword [vga_cursor], 0
    pop rdi
    pop rcx
    pop rax
    ret

; ============================================================
; IDT (Interrupt Descriptor Table)
; ============================================================
; IDT entry: offset_low(16) + selector(16) + ist(8) + type(8) + offset_mid(16) + offset_high(32) + reserved(32)
; Total: 16 bytes per entry

align 16
idt:
; Generate 256 default entries
%assign i 0
%rep 256
    dw 0                    ; offset low (patched at init)
    dw 0x08                 ; code segment selector
    db 0                    ; IST
    db 0x8E                 ; present, ring0, interrupt gate
    dw 0                    ; offset mid
    dd 0                    ; offset high
    dd 0                    ; reserved
%assign i i+1
%endrep
idt_end:

idt_ptr:
    dw idt_end - idt - 1
    dq idt

; Helper: set IDT entry N to handler address
; rdi = idt entry address, rax = handler address
%macro set_idt 2
    lea rax, [rel %2]
    mov rdi, idt + %1 * 16
    mov [rdi], ax
    shr rax, 16
    mov [rdi+4], al
    mov [rdi+7], ah
    bswap rax
    mov [rdi+8], eax
%endmacro

idt_init:
    push rax
    push rcx
    push rdi

    ; Set keyboard ISR (IRQ1 -> vector 33)
    set_idt 33, isr_kb

    ; Set timer ISR (IRQ0 -> vector 32)
    set_idt 32, isr_timer

    lidt [idt_ptr]

    pop rdi
    pop rcx
    pop rax
    ret

; ============================================================
; ISR stubs (must be in same code segment)
; ============================================================

align 8
isr_default:
    push rax
    push rcx
    push rdx
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11

    ; Get interrupt vector
    mov rax, [rsp + 9*8]    ; error code pushed by CPU for some exceptions
    ; Actually just save context and return
    mov rdi, [vga_cursor]
    add rdi, 0xB8000
    mov rsi, msg_isr
    call vga_puts_w_regs

    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rax
    iretq

vga_puts_w_regs:
    ; same as vga_puts but uses regs already on stack
    ; For simplicity just print fixed message
    ret

isr_kb:
    push rax
    push rdi
    push rsi
    in al, 0x60            ; read scancode from PS/2
    mov byte [kb_last], al

    ; Track shift state
    cmp al, 0x2A
    je .shift_on
    cmp al, 0x36
    je .shift_on
    cmp al, 0xAA
    je .shift_off
    cmp al, 0xB6
    je .shift_off

    ; Store in buffer (only make codes, not break codes)
    test al, 0x80
    jnz .eoi
    movzx rdi, byte [kb_head]
    mov byte [kb_buf + rdi], al
    inc byte [kb_head]
    jmp .eoi

.shift_on:
    mov byte [shift_state], 1
    jmp .eoi
.shift_off:
    mov byte [shift_state], 0
.eoi:
    ; Send EOI to PIC
    mov al, 0x20
    out 0x20, al
    pop rsi
    pop rdi
    pop rax
    iretq

isr_timer:
    push rax
    push rdi
    inc qword [timer_count]
    ; Send EOI
    mov al, 0x20
    out 0x20, al
    pop rdi
    pop rax
    iretq

msg_isr: db "ISR!", 0

; ============================================================
; Keyboard Driver
; ============================================================

kb_buf: times 256 db 0
kb_head: db 0
kb_tail: db 0
kb_last: db 0
shift_state: db 0
ctrl_state: db 0

kb_init:
    push rax
    push rdi
    ; Clear buffer
    mov byte [kb_head], 0
    mov byte [kb_tail], 0
    mov byte [kb_last], 0
    mov rdi, kb_buf
    mov rcx, 256
    xor al, al
    rep stosb
    pop rdi
    pop rax
    ret

; Returns char in al, or 0 if no key
kb_getchar:
    push rdx
    push rsi
    push rdi
    xor eax, eax
    mov dl, [kb_head]
    cmp dl, [kb_tail]
    je .done
    movzx rsi, byte [kb_tail]
    mov al, [kb_buf + rsi]
    inc byte [kb_tail]
    ; Convert scancode to ASCII (simple US layout)
    call scancode_to_ascii
.done:
    pop rdi
    pop rsi
    pop rdx
    ret

; scancode_to_ascii: convert scancode to ASCII
; in: al = scancode (make code, bit 7 clear)
; out: al = ASCII character, or 0 if unmapped
scancode_to_ascii:
    push rsi
    push rbx
    movzx rax, al
    cmp al, 128
    jae .none
    cmp byte [shift_state], 0
    je .unshifted
    mov rbx, scancode_shifted
    jmp .lookup
.unshifted:
    mov rbx, scancode_unshifted
.lookup:
    add rbx, rax
    xor al, al
    mov al, [rbx]
    pop rbx
    pop rsi
    ret
.none:
    xor al, al
    pop rbx
    pop rsi
    ret

scancode_unshifted:
    db 0,0,'1','2','3','4','5','6','7','8','9','0','-','=',0,0
    db 'q','w','e','r','t','y','u','i','o','p','[',']',0,0
    db 'a','s','d','f','g','h','j','k','l',';',39,0,0
    db '\','z','x','c','v','b','n','m',',','.','/',0,'*',0,' ',0
    times 134 db 0

scancode_shifted:
    db 0,0,'!','@','#','$','%','^','&','*','(',')','_','+',0,0
    db 'Q','W','E','R','T','Y','U','I','O','P','{','}',0,0
    db 'A','S','D','F','G','H','J','K','L',':','"',0,0
    db '|','Z','X','C','V','B','N','M','<','>','?',0,'*',0,' ',0
    times 134 db 0

; ============================================================
; Memory Manager (bump allocator)
; ============================================================

heap_start: dq kernel_end    ; heap starts right after kernel
heap_ptr:   dq 0
heap_end:   dq 0

; kmalloc: allocate memory
; in: eax = size in bytes
; out: rax = pointer, 0 if OOM
kmalloc:
    push rcx
    push r8
    push rdi

    mov r8d, eax           ; save original size (lower 32 bits)
    ; Align to 16 bytes
    mov eax, r8d
    add eax, 15
    and eax, ~15
    mov ecx, eax           ; rcx = aligned size

    ; Init heap on first call
    mov rax, [heap_ptr]
    test rax, rax
    jnz .have_ptr
    mov rax, [heap_start]
    mov [heap_ptr], rax
    lea rdi, [rax + 0x100000]
    mov [heap_end], rdi
.have_ptr:
    lea rdi, [rax + rcx]
    cmp rdi, [heap_end]
    ja .oom

    push rax               ; save allocation pointer
    mov [heap_ptr], rdi    ; advance heap

    ; Zero the allocated block
    xor al, al
    mov rdi, rax
    mov ecx, r8d           ; original size (not aligned)
    rep stosb

    pop rax                ; return pointer
    pop rdi
    pop r8
    pop rcx
    ret

.oom:
    xor eax, eax
    pop rdi
    pop r8
    pop rcx
    ret

; kfree: not implemented (bump allocator)
kfree:
    ret

; ============================================================
; PIT Timer
; ============================================================

timer_count: dq 0

timer_init:
    push rax
    push rdx
    ; Set PIT to ~100Hz (1193182 / 100 = 11931)
    mov al, 0x36
    out 0x43, al
    mov ax, 11931
    out 0x40, al
    mov al, ah
    out 0x40, al
    pop rdx
    pop rax
    ret

; ============================================================
; PIC (8259A) Initialization
; ============================================================

pic_init:
    push rax
    mov al, 0x11           ; ICW1: edge triggered, cascade, ICW4 needed
    out 0x20, al           ; master PIC
    out 0xA0, al           ; slave PIC

    mov al, 0x20           ; ICW2: IRQ0 -> vector 32
    out 0x21, al
    mov al, 0x28           ; IRQ8 -> vector 40
    out 0xA1, al

    mov al, 4              ; ICW3: slave on IRQ2
    out 0x21, al
    mov al, 2
    out 0xA1, al

    mov al, 1              ; ICW4: 8086 mode
    out 0x21, al
    out 0xA1, al

    mov al, 0              ; OCW1: unmask all
    out 0x21, al
    out 0xA1, al
    pop rax
    ret

; ============================================================
; SHELL
; ============================================================

shell_start:
    push rax
    push rdi
    push rsi

    call pic_init
    call idt_init

    mov rdi, [vga_cursor]
    add rdi, 0xB8000
    mov rsi, prompt
    call vga_puts

    ; Enable interrupts
    sti

.shell_loop:
    ; Wait for a key
    call kb_getchar
    test al, al
    jz .shell_loop

    ; Echo char
    call vga_putchar

    ; Check for enter
    cmp al, 13
    je .enter
    cmp al, 10
    je .enter

    ; Store in line buffer
    movzx rdi, byte [line_len]
    mov byte [line_buf + rdi], al
    inc byte [line_len]
    cmp byte [line_len], 63
    jb .shell_loop

.enter:
    ; Process command
    call process_command

    ; Print prompt again
    mov rdi, [vga_cursor]
    add rdi, 0xB8000
    mov rsi, prompt
    call vga_puts

    jmp .shell_loop

process_command:
    push rax
    push rdi
    push rsi

    mov byte [line_buf + 63], 0   ; ensure null terminator

    ; Check for HELP
    mov rsi, cmd_help
    mov rdi, line_buf
    call strcmp
    test al, al
    jnz .cmd_help

    mov rsi, cmd_info
    mov rdi, line_buf
    call strcmp
    test al, al
    jnz .cmd_info

    mov rsi, cmd_clear
    mov rdi, line_buf
    call strcmp
    test al, al
    jnz .cmd_clear

    mov rsi, cmd_echo
    mov rdi, line_buf
    call strcmp_prefix
    test al, al
    jnz .cmd_echo

    mov rsi, cmd_dir
    mov rdi, line_buf
    call strcmp
    test al, al
    jnz .cmd_dir

    mov rsi, cmd_type
    mov rdi, line_buf
    call strcmp_prefix
    test al, al
    jnz .cmd_type

    ; Unknown command
    mov rdi, [vga_cursor]
    add rdi, 0xB8000
    mov rsi, msg_unknown
    call vga_puts
    call vga_newline
    jmp .done

.cmd_help:
    mov rdi, [vga_cursor]
    add rdi, 0xB8000
    mov rsi, help_text
    call vga_puts
    call vga_newline
    jmp .done

.cmd_info:
    mov rdi, [vga_cursor]
    add rdi, 0xB8000
    mov rsi, info_text
    call vga_puts
    call vga_newline
    ; Print timer count
    mov rsi, msg_ticks
    call vga_puts
    mov rax, [timer_count]
    call vga_puthex
    call vga_newline
    jmp .done

.cmd_clear:
    call vga_clear
    jmp .done

.cmd_echo:
    ; Print rest of line after "ECHO "
    mov rdi, line_buf + 5
    mov byte [rdi + 59], 0
    mov rsi, rdi
    call vga_puts
    call vga_newline
    jmp .done

.cmd_dir:
    call fs_list
    call vga_newline
    jmp .done

.cmd_type:
    ; TYPE filename
    ; Convert "TYPE NAME.EXT" -> 11-byte FAT name (8+3, space-padded)
    call name_to_fat11
    test al, al
    jz .type_bad

    ; Read file into file_buf
    mov rsi, file_name_buf
    mov rdi, file_buf
    mov rdx, 2048
    call fs_read_file
    cmp rax, -1
    je .type_not_found

    ; Null-terminate and print
    mov byte [file_buf + rax], 0
    mov rsi, file_buf
    call vga_puts
    call vga_newline
    jmp .done

.type_bad:
.type_not_found:
    mov rsi, msg_not_found
    call vga_puts
    call vga_newline
    jmp .done

.done:
    mov byte [line_len], 0
    mov byte [line_buf], 0
    pop rsi
    pop rdi
    pop rax
    ret

; strcmp: compares rsi and rdi null-terminated strings
; returns al=1 if equal, 0 if not
strcmp:
    push rcx
    push rsi
    push rdi
.next:
    mov al, [rsi]
    mov cl, [rdi]
    cmp al, cl
    jne .diff
    test al, al
    jz .same
    inc rsi
    inc rdi
    jmp .next
.diff:
    xor al, al
    jmp .done
.same:
    mov al, 1
.done:
    pop rdi
    pop rsi
    pop rcx
    ret

; strcmp_prefix: checks if rsi is a prefix of rdi
; returns al=1 if yes, 0 if no
strcmp_prefix:
    push rcx
    push rsi
    push rdi
.next:
    mov al, [rsi]
    test al, al
    jz .match
    mov cl, [rdi]
    cmp al, cl
    jne .no
    inc rsi
    inc rdi
    jmp .next
.match:
    mov al, 1
    jmp .done
.no:
    xor al, al
.done:
    pop rdi
    pop rsi
    pop rcx
    ret

; Convert text to FAT11 filename (8+3 space-padded, uppercase)
; Input: line_buf + 5 (after TYPE or DIR command)
; Output: file_name_buf (11 bytes)
; Returns al=1 if valid, al=0 if empty
name_to_fat11:
    push rcx
    push rsi
    push rdi
    push rax

    mov rsi, line_buf + 5
    mov rdi, file_name_buf
    mov rcx, 11
    ; Fill with spaces first
    mov al, ' '
    rep stosb

    ; Parse name part (up to 8 chars or dot)
    mov rdi, file_name_buf
    mov rcx, 8
.name_loop:
    lodsb
    test al, al
    jz .done_name
    cmp al, 13
    je .done_name
    cmp al, 10
    je .done_name
    cmp al, '.'
    je .got_dot
    ; Uppercase
    cmp al, 'a'
    jb .store_name
    cmp al, 'z'
    ja .store_name
    sub al, 32
.store_name:
    stosb
    dec rcx
    jnz .name_loop
    ; Filled 8 chars, skip to extension
    jmp .find_ext

.done_name:
    test rcx, rcx
    jnz .valid     ; no extension, just name
    ; We filled exactly 8 chars, check for extension
    ; fall through to find_ext
.find_ext:
    ; Skip to extension
    mov rcx, 3
    mov rdi, file_name_buf + 8
.ext_loop:
    lodsb
    test al, al
    jz .valid
    cmp al, 13
    je .valid
    cmp al, 10
    je .valid
    cmp al, '.'
    ; Ignore dot (we already passed it)
    stosb
    dec rcx
    jnz .ext_loop
    jmp .valid

.got_dot:
    ; Switch to extension (3 bytes at offset 8)
    mov rdi, file_name_buf + 8
    mov rcx, 3
    jmp .ext_loop

.valid:
    mov al, 1
    jmp .done
.empty:
    xor al, al
.done:
    pop rcx          ; discard saved rax
    pop rdi
    pop rsi
    pop rcx
    ret

; ============================================================
; DATA
; ============================================================

line_buf: times 64 db 0
line_len: db 0
file_name_buf: times 12 db 0
file_buf: times 2048 db 0     ; buffer for file contents

prompt:    db "kronos64> ", 0
msg_unknown: db "Unknown command. Type HELP", 0
msg_not_found: db "Not found", 0
msg_fs_init: db "[FS] Loading FAT12...", 0
msg_ok:    db " OK", 0
cmd_help:  db "HELP", 0
cmd_info:  db "INFO", 0
cmd_clear: db "CLEAR", 0
cmd_echo:  db "ECHO ", 0
cmd_dir:   db "DIR", 0
cmd_type:  db "TYPE ", 0
help_text: db "Commands: HELP, INFO, CLEAR, ECHO, DIR, TYPE <file>", 0
info_text: db "KronOS64 - x86-64 Long Mode OS", 0
msg_ticks: db "Ticks: ", 0
banner:    db "KronOS64 v0.1 - x86-64 Unix-like OS", 10
           db "Type HELP for commands", 10, 0

; Stack
align 16
stack_bottom:
    times 4096 db 0
stack_top:

; Mark end of kernel binary - heap starts here
align 16
kernel_end:
