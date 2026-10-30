bits 32
section .text
global _start
_start:
    mov ax, cs
    and eax, 3
    cmp eax, 3
    jne failed
    test esp, 15
    jnz failed
    cmp dword [esp], 1
    jne failed
    mov eax, [esp + 4]
    cmp dword [eax], 'init'
    jne failed
    cmp byte [eax + 4], 0
    jne failed
    cmp dword [esp + 8], 0
    jne failed
    cmp dword [esp + 12], 0
    jne failed
    cmp dword [value], 0x12345678
    jne failed
    cmp dword [zero], 0
    jne failed
    mov dword [zero], 1
    mov eax, 4
    mov ebx, 1
    mov ecx, message
    mov edx, message_size
    int 0x80
    cmp eax, message_size
    jne failed
    xor ebx, ebx
    jmp exit
failed:
    mov ebx, 1
exit:
    mov eax, 1
    int 0x80
    ud2

section .rodata
message: db 'Hello from ring 3', 10
message_size equ $ - message
section .data
value: dd 0x12345678
section .bss
zero: resd 1
section .note.GNU-stack noalloc noexec nowrite progbits
