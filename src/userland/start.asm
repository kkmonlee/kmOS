bits 32
section .text
global _start
extern main
_start:
    xor ebp, ebp
    mov edx, esp
    mov eax, [edx]
    lea ecx, [edx + 4]
    lea ebx, [ecx + eax * 4 + 4]
    and esp, -16
    sub esp, 4
    push ebx
    push ecx
    push eax
    call main
    mov ebx, eax
    mov eax, 1
    int 0x80
    ud2
section .note.GNU-stack noalloc noexec nowrite progbits
