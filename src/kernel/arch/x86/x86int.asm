extern exception_handler, isr_timer_int

section .text

global load_gdt
load_gdt:
    mov eax, [esp + 4]
    lgdt [eax]
    jmp 0x08:.reload
.reload:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov ax, 0x38
    ltr ax
    ret

%macro SAVE_REGS 0
    pushad
    xor eax, eax
    mov ax, ds
    push eax
    mov ax, es
    push eax
    mov ax, fs
    push eax
    mov ax, gs
    push eax
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    cld
%endmacro

%macro RESTORE_REGS 0
    pop gs
    pop fs
    pop es
    pop ds
    popad
%endmacro

%macro EXCEPTION 1
global exception_%1
exception_%1:
%if %1 != 8 && %1 != 10 && %1 != 11 && %1 != 12 && %1 != 13 && %1 != 14 && %1 != 17 && %1 != 21 && %1 != 29 && %1 != 30
    push dword 0
%endif
    push dword %1
    jmp exception_common
%endmacro

%assign vector 0
%rep 256
    EXCEPTION vector
%assign vector vector + 1
%endrep

exception_common:
    SAVE_REGS
    mov ebp, esp
    and esp, -16
    sub esp, 12
    push ebp
    call exception_handler
    mov esp, ebp
    RESTORE_REGS
    add esp, 8
    iret

global _asm_int_32
_asm_int_32:
    SAVE_REGS
    mov ebp, esp
    and esp, -16
    sub esp, 12
    push ebp
    call isr_timer_int
    mov esp, eax
    mov al, 0x20
    out 0x20, al
    RESTORE_REGS
    iret

global test_gp, test_gp_fault, test_gp_resume
test_gp:
    mov ax, 0xfff8
test_gp_fault:
    mov ds, ax
test_gp_resume:
    ret

section .rodata align=4
global exception_stubs
exception_stubs:
%assign vector 0
%rep 256
    dd exception_%+vector
%assign vector vector + 1
%endrep

section .note.GNU-stack noalloc noexec nowrite progbits
