section .data
global task_saved_esp
global task_switch_pending
global task_new_esp
global syscall_handler_asm
task_saved_esp: dd 0
task_switch_pending: dd 0
task_new_esp: dd 0
current_irq: dd 0
irq_stack: times 256 dd 0
irq_stack_ptr: dd irq_stack + 1024

section .text

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

extern isr_handler_c
extern irq_handler_c
extern syscall_handler

isr_common_stub:
    pushad
    mov ax, ds
    push eax
    mov ax, es
    push eax

    mov ax, 0x10
    mov ds, ax
    mov es, ax

    mov eax, [esp + 40]
    push eax
    call isr_handler_c
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

    mov [task_saved_esp], esp

    mov eax, [esp + 40]
    mov [current_irq], eax

    mov esp, [irq_stack_ptr]

    push dword [current_irq]
    call irq_handler_c
    add esp, 4

    cmp dword [task_switch_pending], 0
    je .restore
    mov esp, [task_new_esp]
    mov dword [task_switch_pending], 0
    jmp .eoi
.restore:
    mov esp, [task_saved_esp]
.eoi:
    cmp dword [current_irq], 40
    jl .master
    mov al, 0x20
    out 0xA0, al
.master:
    mov al, 0x20
    out 0x20, al

    pop eax
    mov es, ax
    pop eax
    mov ds, ax
    popad
    add esp, 8
    iret

syscall_handler_asm:
    cli
    pushad
    mov ax, ds
    push eax
    mov ax, es
    push eax

    mov ax, 0x10
    mov ds, ax
    mov es, ax

    push edi
    push esi
    push edx
    push ecx
    push ebx
    push eax
    call syscall_handler
    add esp, 24

    mov dword [esp + 36], eax

    pop eax
    mov es, ax
    pop eax
    mov ds, ax
    popad
    sti
    iret
