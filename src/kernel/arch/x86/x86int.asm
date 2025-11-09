extern isr_default_int, do_syscalls, isr_schedule_int, keyboard_interrupt_handler
extern isr_timer_int

%macro SAVE_REGS 0
    pushad                  ; save all general-purpose registers
    push ds
    push es
    push fs
    push gs
    push ebx
    mov bx, 0x10            ; load data segment selector
    mov ds, bx
    pop ebx
%endmacro

%macro RESTORE_REGS 0
    pop gs
    pop fs
    pop es
    pop ds
    popad                  ; restore all general-purpose registers
%endmacro

%macro INTERRUPT 1
global _asm_int_%1
_asm_int_%1:
    SAVE_REGS
    push %1                 ; push interrupt number onto the stack
    call isr_default_int    ; call default interrupt handler
    pop eax                 ; remove the interrupt number from the stack
    mov al, 0x20
    out 0x20, al            ; send end-of-interrupt signal to PIC
    RESTORE_REGS
    iret
%endmacro

; Timer interrupt (IRQ0 = vector 32)
global _asm_int_32
_asm_int_32:
    SAVE_REGS
    call isr_timer_int
    mov al, 0x20
    out 0x20, al            ; send EOI to PIC
    RESTORE_REGS
    iret

; Keyboard interrupt (IRQ1 = vector 33) - defined in isr_kbd.asm

extern isr_GP_exc, isr_PF_exc 
global _asm_exc_GP, _asm_exc_PF

; Syscall handler - disabled until do_syscalls is implemented
; global _asm_syscalls
; _asm_syscalls:
;     SAVE_REGS
;     push eax                ; pass syscall number
;     call do_syscalls
;     pop eax
;     cli                     ; clear interrupts
;     sti                     ; enable interrupts
;     RESTORE_REGS
;     iret

_asm_exc_GP:
    SAVE_REGS
    call isr_GP_exc
    RESTORE_REGS
    add esp, 4              ; adjust stack pointer
    iret

_asm_exc_PF:
    SAVE_REGS
    push esp                ; pass register state to handler
    call isr_PF_exc
    add esp, 4              ; remove pushed esp
    RESTORE_REGS
    add esp, 4              ; adjust stack pointer (remove error code)
    iret

global _asm_schedule
_asm_schedule:
    SAVE_REGS
    call isr_schedule_int   ; invoke scheduler
    mov al, 0x20
    out 0x20, al            ; send end-of-interrupt signal to PIC
    RESTORE_REGS
    iret

INTERRUPT 1                ; define ISR for interrupt vector 1
INTERRUPT 2                ; define ISR for interrupt vector 2

; Keyboard ISR is implemented in separate file isr_kbd.asm
