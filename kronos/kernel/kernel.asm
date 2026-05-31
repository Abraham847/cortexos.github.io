; ===========================================================================
; KRONOS v0.1 - x86 Kernel
; ===========================================================================

; ---------------------------------------------------------------------------
; ENTRY POINT (16-bit real mode)
; ---------------------------------------------------------------------------
section .text.start
[bits 16]
global start

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    mov [boot_drive], dl

    call enable_a20

    lgdt [gdt_desc]

    mov eax, cr0
    or eax, 1
    mov cr0, eax

    jmp 0x08:pmode_init

enable_a20:
    mov ax, 0x2401
    int 0x15
    ret

boot_drive db 0

; ---------------------------------------------------------------------------
; GDT
; ---------------------------------------------------------------------------
section .data
gdt_start:
    dq 0
gdt_code:
    dw 0xFFFF, 0
    db 0, 10011010b, 11001111b, 0
gdt_data:
    dw 0xFFFF, 0
    db 0, 10010010b, 11001111b, 0
gdt_end:

gdt_desc:
    dw gdt_end - gdt_start - 1
    dd gdt_start

; ---------------------------------------------------------------------------
; PROTECTED MODE ENTRY (32-bit)
; ---------------------------------------------------------------------------
section .text
[bits 32]

pmode_init:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000

    call vga_init
    call idt_init
    call kb_init
    call pit_init

    sti

    mov esi, welcome_msg
    call vga_puts

    call shell_main

    cli
    hlt

welcome_msg db 'KronOS v0.1 - x86 Assembly OS', 0x0D, 0x0A, 'Type HELP for commands', 0x0D, 0x0A, 0

; ===========================================================================
; UTILITY FUNCTIONS
; ===========================================================================
; memset(di, al, cx) - Fill CX bytes at DI with AL
memset:
    push di
    rep stosb
    pop di
    ret

; memcpy(di, si, cx) - Copy CX bytes from SI to DI
memcpy:
    push si
    push di
    rep movsb
    pop di
    pop si
    ret

; strlen(si) -> ax
strlen:
    push si
    xor ax, ax
.loop:
    lodsb
    or al, al
    jz .done
    inc ax
    jmp .loop
.done:
    pop si
    ret

; strcmp(si, di) -> ax (0=equal)
strcmp:
    push si
    push di
.loop:
    lodsb
    mov ah, [di]
    inc di
    sub al, ah
    jnz .done
    cmp ah, 0
    jz .done
    jmp .loop
.done:
    pop di
    pop si
    ret

; itoa(eax) -> prints decimal to screen
itoa:
    pushad
    mov ebx, 10
    xor cx, cx
    test eax, eax
    jnz .loop
    mov al, '0'
    call vga_putchar
    jmp .done2
.loop:
    xor edx, edx
    div ebx
    push dx
    inc cx
    test eax, eax
    jnz .loop
.print:
    pop ax
    add al, '0'
    call vga_putchar
    dec cx
    jnz .print
.done2:
    popad
    ret

; itohex(eax) - Print eax as 8-digit hex
itohex:
    pushad
    mov ecx, 8
.loop:
    rol eax, 4
    push eax
    and al, 0x0F
    cmp al, 10
    jl .digit
    add al, 'A' - 10
    jmp .print
.digit:
    add al, '0'
.print:
    call vga_putchar
    pop eax
    dec ecx
    jnz .loop
    popad
    ret

; atoi(si) -> eax
atoi:
    xor eax, eax
    xor edx, edx
.loop:
    lodsb
    cmp al, '0'
    jb .done
    cmp al, '9'
    ja .done
    sub al, '0'
    imul edx, 10
    add edx, eax
    jmp .loop
.done:
    mov eax, edx
    ret

; itobin(eax) - Print eax as 32-bit binary
itobin:
    pushad
    mov ecx, 32
.loop:
    rol eax, 1
    push eax
    and al, 1
    add al, '0'
    call vga_putchar
    pop eax
    test ecx, 7
    jnz .no_space
    cmp ecx, 1
    je .no_space
    mov al, ' '
    call vga_putchar
.no_space:
    dec ecx
    jnz .loop
    popad
    ret

; ===========================================================================
; VGA TEXT MODE DRIVER
; ===========================================================================
; Variables
vga_row: db 0
vga_col: db 0
vga_fg: db 0x07    ; Light gray
vga_bg: db 0x00    ; Black
cursor_visible: db 1

vga_init:
    pushad
    mov byte [vga_row], 0
    mov byte [vga_col], 0
    mov byte [vga_fg], 0x07
    mov byte [vga_bg], 0x00
    call vga_clear
    call vga_update_cursor
    popad
    ret

vga_clear:
    pushad
    mov edi, 0xB8000
    mov ah, [vga_bg]
    shl ah, 4
    or ah, [vga_fg]
    mov al, ' '
    mov cx, 2000
    rep stosw
    mov byte [vga_row], 0
    mov byte [vga_col], 0
    call vga_update_cursor
    popad
    ret

vga_scroll:
    pushad
    mov edi, 0xB8000
    mov esi, 0xB8000 + 160
    mov ecx, 1920
    rep movsd
    mov edi, 0xB8000 + 3840
    mov ah, [vga_bg]
    shl ah, 4
    or ah, [vga_fg]
    mov al, ' '
    mov cx, 80
    rep stosw
    popad
    ret

vga_putchar:
    pushad
    cmp al, 0x0D
    je .cr
    cmp al, 0x0A
    je .lf
    cmp al, 0x08
    je .bs
    cmp al, 0x09
    je .tab

    movzx edi, byte [vga_row]
    imul edi, 160
    movzx eax, byte [vga_col]
    lea edi, [edi + eax * 2]
    add edi, 0xB8000

    pop eax
    push eax

    mov ah, [vga_bg]
    shl ah, 4
    or ah, [vga_fg]
    stosw

    inc byte [vga_col]
    cmp byte [vga_col], 80
    jb .done
    mov byte [vga_col], 0
    inc byte [vga_row]
    cmp byte [vga_row], 25
    jb .done
    dec byte [vga_row]
    call vga_scroll
    jmp .done

.cr:
    mov byte [vga_col], 0
    jmp .done
.lf:
    inc byte [vga_row]
    cmp byte [vga_row], 25
    jb .done
    dec byte [vga_row]
    call vga_scroll
    jmp .done
.bs:
    cmp byte [vga_col], 0
    jz .done
    dec byte [vga_col]
    jmp .done
.tab:
    mov al, ' '
    call vga_putchar
    test byte [vga_col], 7
    jnz .tab
    jmp .done
.done:
    call vga_update_cursor
    popad
    ret

vga_puts:
    pushad
    mov esi, eax
.loop:
    lodsb
    or al, al
    jz .done
    call vga_putchar
    jmp .loop
.done:
    popad
    ret

vga_update_cursor:
    pushad
    movzx eax, byte [vga_row]
    movzx ebx, byte [vga_col]
    imul eax, 80
    add eax, ebx

    mov dx, 0x3D4
    mov al, 0x0F
    out dx, al
    inc dx
    out dx, al
    dec dx

    mov al, 0x0E
    out dx, al
    inc dx
    xchg ah, al
    out dx, al
    popad
    ret

vga_set_color:
    pushad
    mov [vga_fg], al
    mov [vga_bg], ah
    popad
    ret

; vga_printf - format string with args
; Calling convention: EAX = format string pointer
; Args are passed on the stack in standard cdecl order
; Supports: %s, %d, %x
vga_printf:
    pushad
    mov esi, eax        ; Format string in ESI
    lea edx, [esp + 36] ; First variadic arg after pushad + ret addr
.loop:
    lodsb
    or al, al
    jz .done
    cmp al, '%'
    jne .print
    lodsb
    or al, al
    jz .done
    cmp al, 's'
    je .str
    cmp al, 'd'
    je .dec
    cmp al, 'x'
    je .hex
    jmp .print
.str:
    mov eax, [edx]
    add edx, 4
    pushad
    push esi
    call vga_puts
    pop esi
    popad
    jmp .loop
.dec:
    mov eax, [edx]
    add edx, 4
    pushad
    push esi
    call itoa
    pop esi
    popad
    jmp .loop
.hex:
    mov eax, [edx]
    add edx, 4
    pushad
    push esi
    call itohex
    pop esi
    popad
    jmp .loop
.print:
    call vga_putchar
    jmp .loop
.done:
    popad
    ret

; ===========================================================================
; IDT, ISRs, and IRQs
; ===========================================================================
; IDT entry structure
idt_entries:
    times 256 * 8 db 0

; IDT descriptor
idt_desc:
    dw 256 * 8 - 1
    dd idt_entries

; ISR macros
%macro ISR_NOERR 1
global isr%1
isr%1:
    push 0
    push %1
    jmp isr_common_stub
%endmacro

%macro ISR_ERR 1
global isr%1
isr%1:
    push %1
    jmp isr_common_stub
%endmacro

%macro IRQ 2
global irq%1
irq%1:
    push 0
    push %2
    jmp irq_common_stub
%endmacro

; CPU exceptions (0-31)
ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR 8
ISR_NOERR 9
ISR_ERR 10
ISR_ERR 11
ISR_ERR 12
ISR_ERR 13
ISR_ERR 14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR 17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_NOERR 30
ISR_NOERR 31

; IRQs (0-15 mapped to 32-47)
IRQ 0, 32
IRQ 1, 33
IRQ 2, 34
IRQ 3, 35
IRQ 4, 36
IRQ 5, 37
IRQ 6, 38
IRQ 7, 39
IRQ 8, 40
IRQ 9, 41
IRQ 10, 42
IRQ 11, 43
IRQ 12, 44
IRQ 13, 45
IRQ 14, 46
IRQ 15, 47

; ISR names for exception messages
exc_msgs:
    dd .msg0, .msg1, .msg2, .msg3, .msg4, .msg5, .msg6, .msg7
    dd .msg8, .msg9, .msg10, .msg11, .msg12, .msg13, .msg14, .msg15
    dd .msg16, .msg17, .msg18, .msg19, .msg20, .msg21, .msg22, .msg23
    dd .msg24, .msg25, .msg26, .msg27, .msg28, .msg29, .msg30, .msg31
.msg0: db 'Division by Zero', 0
.msg1: db 'Debug', 0
.msg2: db 'Non-maskable Interrupt', 0
.msg3: db 'Breakpoint', 0
.msg4: db 'Overflow', 0
.msg5: db 'Bound Range Exceeded', 0
.msg6: db 'Invalid Opcode', 0
.msg7: db 'Device Not Available', 0
.msg8: db 'Double Fault', 0
.msg9: db 'Coprocessor Segment Overrun', 0
.msg10: db 'Invalid TSS', 0
.msg11: db 'Segment Not Present', 0
.msg12: db 'Stack Segment Fault', 0
.msg13: db 'General Protection Fault', 0
.msg14: db 'Page Fault', 0
.msg15: db 'Reserved', 0
.msg16: db 'x87 FPU Error', 0
.msg17: db 'Alignment Check', 0
.msg18: db 'Machine Check', 0
.msg19: db 'SIMD Floating-Point', 0
.msg20: db 'Virtualization', 0
.msg21: db 'Control Protection', 0
.msg22: db 'Reserved', 0
.msg23: db 'Reserved', 0
.msg24: db 'Reserved', 0
.msg25: db 'Reserved', 0
.msg26: db 'Reserved', 0
.msg27: db 'Reserved', 0
.msg28: db 'Hypervisor Injection', 0
.msg29: db 'VMM Communication', 0
.msg30: db 'Security', 0
.msg31: db 'Reserved', 0

isr_common_stub:
    pushad
    mov ax, ds
    push eax
    mov ax, es
    push eax

    mov ax, 0x10
    mov ds, ax
    mov es, ax

    mov eax, [esp + 44]
    push eax
    call isr_handler
    add esp, 4

    pop eax
    mov es, ax
    pop eax
    mov ds, ax
    popad
    add esp, 8
    iret

irq_common_stub:
    pushad
    mov ax, ds
    push eax
    mov ax, es
    push eax

    mov ax, 0x10
    mov ds, ax
    mov es, ax

    mov eax, [esp + 44]
    cmp eax, 40
    jl .eoi_master
    mov al, 0x20
    out 0xA0, al
.eoi_master:
    mov al, 0x20
    out 0x20, al

    mov eax, [esp + 44]
    push eax
    call irq_handler
    add esp, 4

    pop eax
    mov es, ax
    pop eax
    mov ds, ax
    popad
    add esp, 8
    iret

isr_handler:
    pushad
    mov eax, [esp + 36]
    mov esi, [exc_msgs + eax * 4]
    mov eax, cr2
    push eax
    push esi
    mov eax, exc_fmt
    call vga_printf
    add esp, 8
    popad
    ret

exc_fmt: db 0x0D, 0x0A, '!!! %s (CR2=%x) !!!', 0x0D, 0x0A, 0

irq_handler:
    pushad
    mov eax, [esp + 36]

    cmp eax, 32
    je .timer
    cmp eax, 33
    je .keyboard
    jmp .done

.timer:
    call timer_tick
    jmp .done

.keyboard:
    call kb_handler
    jmp .done

.done:
    popad
    ret

idt_init:
    pushad

    mov edi, idt_entries
    mov ecx, 256
    xor eax, eax
    rep stosd

    call idt_remap_pic

    ; Set up ISRs 0-31
    mov esi, isr_table
    xor ebx, ebx
.loop_isr:
    lodsd
    or eax, eax
    jz .isr_done
    push esi
    push ebx
    push eax
    call idt_set_gate
    add esp, 12
    pop esi
    inc ebx
    jmp .loop_isr
.isr_done:

    ; Set up IRQs 0-15
    mov esi, irq_table
    mov ebx, 32
.loop_irq:
    lodsd
    or eax, eax
    jz .irq_done
    push esi
    push ebx
    push eax
    call idt_set_gate
    add esp, 12
    pop esi
    inc ebx
    jmp .loop_irq
.irq_done:

    lidt [idt_desc]

    popad
    ret

idt_set_gate:
    pushad
    mov edi, [esp + 36]   ; Number
    mov eax, [esp + 40]   ; Handler
    mov bx, [esp + 44]    ; Selector

    shl edi, 3
    add edi, idt_entries

    mov [edi], ax
    mov [edi + 2], bx
    mov word [edi + 4], 0x8E00
    shr eax, 16
    mov [edi + 6], ax

    popad
    ret

idt_remap_pic:
    pushad
    mov al, 0x11
    out 0x20, al
    out 0xA0, al

    mov al, 0x20
    out 0x21, al
    mov al, 0x28
    out 0xA1, al

    mov al, 0x04
    out 0x21, al
    mov al, 0x02
    out 0xA1, al

    mov al, 0x01
    out 0x21, al
    out 0xA1, al

    mov al, 0xFD     ; Enable IRQ0+IRQ1 only
    out 0x21, al
    mov al, 0xFF
    out 0xA1, al

    popad
    ret

isr_table:
    dd isr0, isr1, isr2, isr3, isr4, isr5, isr6, isr7
    dd isr8, isr9, isr10, isr11, isr12, isr13, isr14, isr15
    dd isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23
    dd isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31
    dd 0

irq_table:
    dd irq0, irq1, irq2, irq3, irq4, irq5, irq6, irq7
    dd irq8, irq9, irq10, irq11, irq12, irq13, irq14, irq15
    dd 0

; ===========================================================================
; KEYBOARD DRIVER
; ===========================================================================
kb_buffer: times 256 db 0
kb_head: dd 0
kb_tail: dd 0
kb_shift: db 0
kb_caps: db 0

kb_init:
    pushad
    mov dword [kb_head], 0
    mov dword [kb_tail], 0
    mov byte [kb_shift], 0
    mov byte [kb_caps], 0
    popad
    ret

kb_handler:
    pushad
    in al, 0x60

    cmp al, 0x2A
    je .shift_down
    cmp al, 0x36
    je .shift_down
    cmp al, 0xAA
    je .shift_up
    cmp al, 0xB6
    je .shift_up
    cmp al, 0x3A
    je .caps

    test al, 0x80
    jnz .done

    movzx eax, al
    mov al, [scancode_table + eax]
    cmp al, 0
    je .done

    test byte [kb_shift], 1
    jz .no_shift
    movzx eax, al
    mov al, [scancode_shift + eax]
.no_shift:
    test byte [kb_caps], 1
    jz .store
    cmp al, 'a'
    jb .store
    cmp al, 'z'
    ja .store
    sub al, 32
.store:
    mov edi, [kb_head]
    mov [kb_buffer + edi], al
    inc edi
    and edi, 255
    mov [kb_head], edi
    jmp .done

.shift_down:
    or byte [kb_shift], 1
    jmp .done
.shift_up:
    and byte [kb_shift], 0xFE
    jmp .done
.caps:
    not byte [kb_caps]
    and byte [kb_caps], 1
    jmp .done
.done:
    popad
    ret

kb_getchar:
    pushad
.loop:
    cli
    mov eax, [kb_head]
    cmp eax, [kb_tail]
    jne .have_char
    sti
    nop
    jmp .loop
.have_char:
    mov esi, [kb_tail]
    mov al, [kb_buffer + esi]
    inc esi
    and esi, 255
    mov [kb_tail], esi
    sti
    mov [esp + 28], al
    popad
    ret

kb_gets:
    pushad
    mov edi, [esp + 36]
    xor ecx, ecx
.loop:
    call kb_getchar
    cmp al, 0x0D
    je .enter
    cmp al, 0x08
    je .backspace
    cmp al, 0x09
    je .tab
    cmp al, 0x1B
    je .escape
    cmp cl, 78
    jae .loop
    mov [edi], al
    inc edi
    inc ecx
    push eax
    call vga_putchar
    pop eax
    jmp .loop
.backspace:
    cmp ecx, 0
    jz .loop
    dec edi
    dec ecx
    mov al, 0x08
    call vga_putchar
    mov al, ' '
    call vga_putchar
    mov al, 0x08
    call vga_putchar
    jmp .loop
.tab:
    mov al, ' '
    call vga_putchar
    mov [edi], al
    inc edi
    inc ecx
    jmp .loop
.escape:
    xor ecx, ecx
    mov edi, [esp + 36]
    mov byte [edi], 0
    jmp .done
.enter:
    mov byte [edi], 0
    mov al, 0x0D
    call vga_putchar
.done:
    popad
    ret

scancode_table:
    db 0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 0x08, 0x09
    db 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', 0x0D, 0, 'a', 's'
    db 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', 0x27, '`', 0, '\', 'z', 'x', 'c', 'v'
    db 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ', 0, 0, 0, 0, 0, 0
    db 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    db 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    db 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    db 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    db 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    db 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    db 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    db 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    db 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    db 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    db 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    db 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0

scancode_shift:
    db 0, 0, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', 0x08, 0x09
    db 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', 0x0D, 0, 'A', 'S'
    db 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', 0x22, '~', 0, '|', 'Z', 'X', 'C', 'V'
    db 'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' ', 0, 0, 0, 0, 0, 0
    times 192 db 0

; ===========================================================================
; PIT TIMER AND SCHEDULER
; ===========================================================================
pit_ticks: dd 0
scheduler_enabled: db 0

; Task Control Block
MAX_TASKS equ 8
task_esp: times MAX_TASKS dd 0
task_state: times MAX_TASKS db 0
current_task: dd 0
num_tasks: dd 1

; Task state values
TASK_FREE equ 0
TASK_READY equ 1
TASK_RUNNING equ 2
TASK_DEAD equ 3

; Stack for each task (4096 bytes each)
task_stacks:
    times MAX_TASKS * 4096 db 0

pit_init:
    pushad
    mov byte [pit_ticks], 0
    mov byte [pit_ticks + 1], 0
    mov byte [pit_ticks + 2], 0
    mov byte [pit_ticks + 3], 0

    mov al, 0x36
    out 0x43, al
    mov ax, 11932
    out 0x40, al
    shr ax, 8
    out 0x40, al

    mov dword [current_task], 0
    mov byte [task_state], TASK_RUNNING
    mov dword [num_tasks], 1
    mov byte [scheduler_enabled], 1

    popad
    ret

timer_tick:
    inc dword [pit_ticks]
    call scheduler_tick
    ret

scheduler_tick:
    pushad
    cmp byte [scheduler_enabled], 0
    je .done

    mov esi, [current_task]
    mov [task_esp + esi * 4], esp

.next_task:
    inc esi
    cmp esi, MAX_TASKS
    jb .check
    xor esi, esi
.check:
    cmp byte [task_state + esi], TASK_READY
    jne .next_task

    mov [current_task], esi
    mov byte [task_state + esi], TASK_RUNNING
    mov esp, [task_esp + esi * 4]

.done:
    popad
    ret

; task_create(entry_point - on stack)
task_create:
    pushad
    mov eax, [esp + 36]   ; entry point

    ; Find free task slot
    xor esi, esi
.find:
    cmp byte [task_state + esi], TASK_FREE
    je .found
    inc esi
    cmp esi, MAX_TASKS
    jb .find
    mov dword [esp + 28], -1
    popad
    ret
.found:
    ; Set up stack for this task
    mov edi, task_stacks + (MAX_TASKS - 1) * 4096
    sub edi, esi
    shl edi, 12
    add edi, 4096 - 60

    ; Fake IRQ frame (as if irq_common_stub just happened)
    xor ecx, ecx
    mov [edi + 0], ecx      ; ES = 0
    mov [edi + 4], ecx      ; DS = 0
    mov [edi + 8], ecx      ; EDI = 0
    mov [edi + 12], ecx     ; ESI = 0
    mov [edi + 16], ecx     ; EBP = 0
    mov [edi + 20], ecx     ; ESP = dummy
    mov [edi + 24], ecx     ; EBX = 0
    mov [edi + 28], ecx     ; EDX = 0
    mov [edi + 32], ecx     ; ECX = 0
    mov [edi + 36], ecx     ; EAX = 0
    mov [edi + 40], ecx     ; error code = 0
    mov [edi + 44], ecx     ; int number = 0
    mov [edi + 48], eax     ; EIP = entry point
    mov dword [edi + 52], 0x08   ; CS = kernel code
    mov dword [edi + 56], 0x202  ; EFLAGS (IF set)

    mov [task_esp + esi * 4], edi
    mov byte [task_state + esi], TASK_READY
    inc dword [num_tasks]

    mov [esp + 28], esi     ; Return task ID
    popad
    ret

task_exit:
    pushad
    mov esi, [current_task]
    mov byte [task_state + esi], TASK_DEAD
    dec dword [num_tasks]
    sti
.halt:
    hlt
    jmp .halt

; ===========================================================================
; MEMORY ALLOCATOR
; ===========================================================================
; Simple free list allocator

heap_start: dd 0
heap_end: dd 0

; Memory block header
; Each block: [size:4][next:4][data...]
; size includes header

mem_init:
    pushad
    mov dword [heap_start], mem_pool
    mov dword [heap_end], mem_pool + MEM_POOL_SIZE

    mov edi, mem_pool
    mov eax, MEM_POOL_SIZE
    mov [edi], eax          ; Size
    mov dword [edi + 4], 0  ; Next = NULL

    popad
    ret

kmalloc:
    pushad
    mov ecx, [esp + 36]   ; Size
    add ecx, 7
    and ecx, ~7
    add ecx, 8

    mov esi, [heap_start]
    xor edx, edx

.loop:
    cmp esi, 0
    je .fail
    mov eax, [esi]
    cmp eax, ecx
    jb .next

    ; Found a block. Split if large enough
    sub eax, ecx
    cmp eax, 32
    jb .no_split

    mov [esi], ecx
    push esi
    add esi, ecx
    mov [esi], eax
    mov eax, [esp]
    mov edx, [eax + 4]
    mov [esi + 4], edx
    mov [eax + 4], esi
    pop esi
    jmp .alloc

.no_split:
    ; Remove from free list
    mov eax, [esi + 4]
    test edx, edx
    jz .first
    mov [edx + 4], eax
    jmp .alloc
.first:
    mov [heap_start], eax
    jmp .alloc

.next:
    mov edx, esi
    mov esi, [esi + 4]
    jmp .loop

.alloc:
    add esi, 8
    mov [esp + 28], esi
    popad
    ret

.fail:
    mov dword [esp + 28], 0
    popad
    ret

kfree:
    pushad
    mov esi, [esp + 36]
    sub esi, 8

    ; Add to free list
    mov eax, [heap_start]
    mov [esi + 4], eax
    mov [heap_start], esi

    popad
    ret

MEM_POOL_SIZE equ 16384
mem_pool: times MEM_POOL_SIZE db 0

; ===========================================================================
; SHELL
; ===========================================================================
shell_main:
    pushad

    call vga_clear

    mov eax, logo_ascii
    call vga_puts
    mov al, 0x0D
    call vga_putchar
    mov al, 0x0A
    call vga_putchar

.loop:
    mov eax, prompt_str
    call vga_puts

    lea edi, [cmd_buf]
    push edi
    call kb_gets
    pop edi

    cmp byte [edi], 0
    je .loop

    call execute_cmd

    jmp .loop

execute_cmd:
    pushad
    lea esi, [cmd_buf]

    ; Skip leading spaces
.skip_spaces:
    lodsb
    cmp al, ' '
    je .skip_spaces
    dec esi

    ; Check commands
    mov edi, cmd_help
    call strcmp
    test ax, ax
    jz .do_help

    mov edi, cmd_clear
    call strcmp
    test ax, ax
    jz .do_clear

    mov edi, cmd_echo
    call strcmp
    test ax, ax
    jz .do_echo

    mov edi, cmd_ver
    call strcmp
    test ax, ax
    jz .do_ver

    mov edi, cmd_info
    call strcmp
    test ax, ax
    jz .do_info

    mov edi, cmd_color
    call strcmp
    test ax, ax
    jz .do_color

    mov edi, cmd_calc
    call strcmp
    test ax, ax
    jz .do_calc

    mov edi, cmd_beep
    call strcmp
    test ax, ax
    jz .do_beep

    mov edi, cmd_uptime
    call strcmp
    test ax, ax
    jz .do_uptime

    mov edi, cmd_hex
    call strcmp
    test ax, ax
    jz .do_hex

    mov edi, cmd_bin
    call strcmp
    test ax, ax
    jz .do_bin

    mov edi, cmd_matrix
    call strcmp
    test ax, ax
    jz .do_matrix

    mov edi, cmd_reboot
    call strcmp
    test ax, ax
    jz .do_reboot

    mov edi, cmd_shutdown
    call strcmp
    test ax, ax
    jz .do_shutdown

    mov edi, cmd_theme
    call strcmp
    test ax, ax
    jz .do_theme

    mov edi, cmd_logo
    call strcmp
    test ax, ax
    jz .do_logo

    mov edi, cmd_cowsay
    call strcmp
    test ax, ax
    jz .do_cowsay

    mov edi, cmd_tasks
    call strcmp
    test ax, ax
    jz .do_tasks

    mov edi, cmd_mem
    call strcmp
    test ax, ax
    jz .do_mem

    mov eax, cmd_unknown
    call vga_puts
    jmp .done

.do_help:
    call cmd_help_fn
    jmp .done
.do_clear:
    call cmd_clear_fn
    jmp .done
.do_echo:
    call cmd_echo_fn
    jmp .done
.do_ver:
    call cmd_ver_fn
    jmp .done
.do_info:
    call cmd_info_fn
    jmp .done
.do_color:
    call cmd_color_fn
    jmp .done
.do_calc:
    call cmd_calc_fn
    jmp .done
.do_beep:
    call cmd_beep_fn
    jmp .done
.do_uptime:
    call cmd_uptime_fn
    jmp .done
.do_hex:
    call cmd_hex_fn
    jmp .done
.do_bin:
    call cmd_bin_fn
    jmp .done
.do_matrix:
    call cmd_matrix_fn
    jmp .done
.do_reboot:
    call cmd_reboot_fn
    jmp .done
.do_shutdown:
    call cmd_shutdown_fn
    jmp .done
.do_theme:
    call cmd_theme_fn
    jmp .done
.do_logo:
    call cmd_logo_fn
    jmp .done
.do_cowsay:
    call cmd_cowsay_fn
    jmp .done
.do_tasks:
    call cmd_tasks_fn
    jmp .done
.do_mem:
    call cmd_mem_fn
    jmp .done
.done:
    popad
    ret

; get_arg - returns pointer to Nth arg in esi
get_arg:
    pushad
    mov edx, [esp + 36]
    lea esi, [cmd_buf]
.skip_cmd:
    lodsb
    cmp al, 0
    je .eos
    cmp al, ' '
    jne .skip_cmd
    dec esi

.next_arg:
    lodsb
    cmp al, ' '
    je .next_arg
    dec esi
    dec edx
    jnz .next_arg

    mov [esp + 28], esi
    popad
    ret

.eos:
    xor esi, esi
    mov [esp + 28], esi
    popad
    ret

; --- Command implementations ---
cmd_help_fn:
    pushad
    mov eax, help_text
    call vga_puts
    popad
    ret

cmd_clear_fn:
    pushad
    call vga_clear
    popad
    ret

cmd_echo_fn:
    pushad
    lea esi, [cmd_buf]
.skip:
    lodsb
    cmp al, ' '
    jne .skip
    cmp al, 0
    je .done
    call vga_puts
    mov al, 0x0D
    call vga_putchar
.done:
    popad
    ret

cmd_ver_fn:
    pushad
    mov eax, ver_str
    call vga_puts
    popad
    ret

cmd_info_fn:
    pushad
    mov eax, info_header
    call vga_puts
    mov eax, [pit_ticks]
    push eax
    mov eax, info_fmt
    call vga_printf
    add esp, 4
    mov al, 0x0D
    call vga_putchar
    popad
    ret

info_header:
    db 0x0D, 0x0A, 'KronOS v0.1 Alpha', 0x0D, 0x0A
    db 'Arch: x86 32-bit', 0x0D, 0x0A
    db 'Kernel: Assembly', 0x0D, 0x0A
    db 'Scheduler: Round-Robin', 0x0D, 0x0A, 0
info_fmt: db 'Timer ticks: %d', 0

cmd_color_fn:
    pushad
    push dword 1
    call get_arg
    add esp, 4
    cmp esi, 0
    je .done

    call atoi
    cmp eax, 15
    ja .done
    mov byte [vga_fg], al

    push dword 2
    call get_arg
    add esp, 4
    cmp esi, 0
    je .apply
    call atoi
    cmp eax, 15
    ja .apply
    mov byte [vga_bg], al

.apply:
    mov al, [vga_fg]
    mov ah, [vga_bg]
    call vga_set_color
    call vga_clear
.done:
    popad
    ret

cmd_calc_fn:
    pushad
    push dword 1
    call get_arg
    add esp, 4
    cmp esi, 0
    je .done

    call atoi
    mov ebx, eax

.find_op:
    lodsb
    cmp al, 0
    je .show
    cmp al, '+'
    je .add
    cmp al, '-'
    je .sub
    cmp al, '*'
    je .mul
    cmp al, '/'
    je .div
    jmp .find_op

.add:
    call atoi
    add ebx, eax
    jmp .show
.sub:
    call atoi
    sub ebx, eax
    jmp .show
.mul:
    call atoi
    imul ebx, eax
    jmp .show
.div:
    call atoi
    test eax, eax
    jz .div_zero
    xchg eax, ebx
    xor edx, edx
    div ebx
    mov ebx, eax
    jmp .show
.div_zero:
    mov eax, div_zero_msg
    call vga_puts
    jmp .done
.show:
    mov eax, ebx
    call itoa
    mov al, 0x0D
    call vga_putchar
.done:
    popad
    ret

cmd_beep_fn:
    pushad
    mov al, 0xB6
    out 0x43, al
    mov ax, 1520
    out 0x42, al
    shr ax, 8
    out 0x42, al

    in al, 0x61
    or al, 3
    out 0x61, al

    mov ecx, 2000000
.delay:
    loop .delay

    in al, 0x61
    and al, 0xFC
    out 0x61, al
    popad
    ret

cmd_uptime_fn:
    pushad
    mov eax, [pit_ticks]
    xor edx, edx
    mov ecx, 100
    div ecx
    push eax
    mov eax, uptime_fmt
    call vga_printf
    add esp, 4
    mov al, 0x0D
    call vga_putchar
    popad
    ret

uptime_fmt: db 'Uptime: %d seconds', 0

cmd_hex_fn:
    pushad
    push dword 1
    call get_arg
    add esp, 4
    cmp esi, 0
    je .done
    call atoi
    push eax
    mov eax, hex_fmt
    call vga_printf
    add esp, 4
    mov al, 0x0D
    call vga_putchar
.done:
    popad
    ret

hex_fmt: db '0x%x', 0

cmd_bin_fn:
    pushad
    push dword 1
    call get_arg
    add esp, 4
    cmp esi, 0
    je .done
    call atoi
    call itobin
    mov al, 0x0D
    call vga_putchar
.done:
    popad
    ret

cmd_matrix_fn:
    pushad
    mov eax, matrix_done
    call vga_puts
    popad
    ret

cmd_reboot_fn:
    pushad
    mov al, 0xFE
    out 0x64, al
    popad
    ret

cmd_shutdown_fn:
    pushad
    mov esi, shutdown_msg
    call vga_puts
    cli
    hlt
    popad
    ret

cmd_theme_fn:
    pushad
    inc byte [vga_fg]
    cmp byte [vga_fg], 16
    jb .apply
    mov byte [vga_fg], 1
.apply:
    mov al, [vga_fg]
    mov ah, [vga_bg]
    call vga_set_color
    call vga_clear
    mov eax, theme_msg
    call vga_puts
    popad
    ret

cmd_logo_fn:
    pushad
    mov eax, logo_ascii
    call vga_puts
    mov al, 0x0D
    call vga_putchar
    mov al, 0x0A
    call vga_putchar
    popad
    ret

cmd_cowsay_fn:
    pushad
    push dword 1
    call get_arg
    add esp, 4
    cmp esi, 0
    jne .print
    lea esi, [default_cow]
.print:
    push esi
    mov eax, cow_base
    call vga_puts
    pop esi
    call vga_puts
    mov eax, cow_mid
    call vga_puts
    push esi
    mov eax, cow_fmt
    push eax
    mov eax, esp
    call vga_printf
    add esp, 4
    pop esi
    mov eax, cow_end
    call vga_puts
    push esi
    call strlen
    push eax
    mov eax, cow_len
    push eax
    mov eax, esp
    call vga_printf
    add esp, 8
    mov eax, cow_body
    call vga_puts
    mov al, 0x0D
    call vga_putchar
    mov al, 0x0A
    call vga_putchar
    popad
    ret

cmd_tasks_fn:
    pushad
    xor esi, esi
.loop:
    cmp byte [task_state + esi], TASK_FREE
    je .next
    mov eax, [task_esp + esi * 4]
    push eax
    push esi
    mov eax, task_fmt
    call vga_printf
    add esp, 8
.next:
    inc esi
    cmp esi, MAX_TASKS
    jb .loop
    popad
    ret

task_fmt: db 0x0D, 0x0A, 'Task %d: ESP=%x', 0

cmd_mem_fn:
    pushad
    mov eax, mem_pool + MEM_POOL_SIZE
    push eax
    mov eax, mem_pool
    push eax
    mov eax, mem_fmt
    call vga_printf
    add esp, 8
    mov al, 0x0D
    call vga_putchar
    popad
    ret

mem_fmt: db 'Heap: %x - %x', 0

; ===========================================================================
; DATA SECTION
; ===========================================================================
cmd_buf: times 80 db 0

prompt_str: db 0x0D, 0x0A, 'KronOS> ', 0
cmd_unknown: db 0x0D, 0x0A, 'Unknown command. Type HELP', 0
div_zero_msg: db 'Error: Division by zero', 0
shutdown_msg: db 0x0D, 0x0A, 'Halting system...', 0
theme_msg: db 0x0D, 0x0A, 'Theme changed', 0
matrix_done: db 0x0D, 0x0A, 'Matrix effect not available in this build', 0

cmd_help: db 'HELP', 0
cmd_clear: db 'CLEAR', 0
cmd_echo: db 'ECHO', 0
cmd_ver: db 'VER', 0
cmd_info: db 'INFO', 0
cmd_color: db 'COLOR', 0
cmd_calc: db 'CALC', 0
cmd_beep: db 'BEEP', 0
cmd_uptime: db 'UPTIME', 0
cmd_hex: db 'HEX', 0
cmd_bin: db 'BIN', 0
cmd_matrix: db 'MATRIX', 0
cmd_reboot: db 'REBOOT', 0
cmd_shutdown: db 'SHUTDOWN', 0
cmd_theme: db 'THEME', 0
cmd_logo: db 'LOGO', 0
cmd_cowsay: db 'COWSAY', 0
cmd_tasks: db 'TASKS', 0
cmd_mem: db 'MEM', 0

help_text:
    db 0x0D, 0x0A
    db 'KronOS Shell v0.1', 0x0D, 0x0A
    db '-----------------', 0x0D, 0x0A
    db 'HELP     - Show this help', 0x0D, 0x0A
    db 'CLEAR    - Clear screen', 0x0D, 0x0A
    db 'ECHO     - Print text', 0x0D, 0x0A
    db 'VER      - Show version', 0x0D, 0x0A
    db 'INFO     - System info', 0x0D, 0x0A
    db 'COLOR    - Set colors (0-15)', 0x0D, 0x0A
    db 'CALC     - Calculator (e.g. CALC 2+3*4)', 0x0D, 0x0A
    db 'BEEP     - PC speaker beep', 0x0D, 0x0A
    db 'UPTIME   - System uptime', 0x0D, 0x0A
    db 'HEX      - Convert to hex', 0x0D, 0x0A
    db 'BIN      - Convert to binary', 0x0D, 0x0A
    db 'THEME    - Cycle color themes', 0x0D, 0x0A
    db 'LOGO     - Show KronOS logo', 0x0D, 0x0A
    db 'COWSAY   - Cowsay', 0x0D, 0x0A
    db 'TASKS    - Show tasks', 0x0D, 0x0A
    db 'MEM      - Show memory info', 0x0D, 0x0A
    db 'MATRIX   - Matrix rain (WIP)', 0x0D, 0x0A
    db 'REBOOT   - Reboot system', 0x0D, 0x0A
    db 'SHUTDOWN - Halt system', 0x0D, 0x0A, 0

ver_str: db 0x0D, 0x0A, 'KronOS v0.1 Alpha (assembly)', 0x0D, 0x0A, 0

logo_ascii:
    db 0x0D, 0x0A
    db '  _   _', 0x0D, 0x0A
    db ' | \ | | ___  _ __ ___   ___  _ __', 0x0D, 0x0A
    db ' |  \| |/ _ \| |__/ _ \ / _ \| |__|', 0x0D, 0x0A
    db ' | |\  | (_) | | | (_) | (_) | |', 0x0D, 0x0A
    db ' |_| \_|\___/|_|  \___/ \___/|_|', 0x0D, 0x0A
    db '       v0.1 Alpha', 0x0D, 0x0A, 0

default_cow: db 'Moo', 0

cow_base:
    db 0x0D, 0x0A, ' ---', 0
cow_mid:
    db 0x0D, 0x0A, '< %s >', 0x0D, 0x0A, ' ---', 0
cow_fmt: db 0x0D, 0x0A, '< %s >', 0x0D, 0x0A, ' ---', 0
cow_len: db 0x0D, 0x0A, '        \   ^__^', 0x0D, 0x0A, '         \  (oo)\_______', 0x0D, 0x0A, '            (__)\       )\/\', 0x0D, 0x0A, '                ||----w |', 0x0D, 0x0A, '                ||     ||', 0
cow_end: db ' ---', 0
cow_body:
    db 0x0D, 0x0A, '        \   ^__^', 0x0D, 0x0A
    db '         \  (oo)\_______', 0x0D, 0x0A
    db '            (__)\       )\/\', 0x0D, 0x0A
    db '                ||----w |', 0x0D, 0x0A
    db '                ||     ||', 0
