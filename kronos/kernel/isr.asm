section .data
global task_saved_esp
global task_switch_pending
global task_new_esp
task_saved_esp: dd 0
task_switch_pending: dd 0
task_new_esp: dd 0

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

extern isr_handler_c
extern irq_handler_c

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
    cmp eax, 40
    jl .master
    mov al, 0x20
    out 0xA0, al
.master:
    mov al, 0x20
    out 0x20, al

    mov eax, [esp + 40]
    push eax
    call irq_handler_c
    add esp, 4

    cmp dword [task_switch_pending], 0
    je .no_switch
    mov esp, [task_new_esp]
    mov dword [task_switch_pending], 0
.no_switch:

    pop eax
    mov es, ax
    pop eax
    mov ds, ax
    popad
    add esp, 8
    iret
