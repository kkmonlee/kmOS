global _asm_int_33
extern keyboard_interrupt_handler

section .text

_asm_int_33:
    pushad
    push ds
    push es
    push fs
    push gs

    ; set kernel data segment (assumes 0x10)
    mov ax, 0x10
    mov ds, ax
    mov es, ax

    call keyboard_interrupt_handler

    mov al, 0x20
    out 0x20, al

    pop gs
    pop fs
    pop es
    pop ds
    popad
    iret
