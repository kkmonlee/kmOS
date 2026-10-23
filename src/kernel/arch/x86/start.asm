global _start, _kmain, boot_stack_bottom, boot_stack_top
extern kmain, start_ctors, end_ctors, sbss, ebss

section .multiboot align=4
dd 0x1BADB002
dd 0x00000003
dd -(0x1BADB002 + 0x00000003)

section .text
_start:
_kmain:
    cli
    cld
    mov esi, eax
    mov ebp, ebx
    mov edi, sbss
    mov ecx, ebss
    sub ecx, edi
    xor eax, eax
    rep stosb
    mov esp, boot_stack_top
    and esp, -16
    mov ebx, start_ctors
.ctors:
    cmp ebx, end_ctors
    jae .main
    call [ebx]
    add ebx, 4
    jmp .ctors
.main:
    sub esp, 8
    push ebp
    push esi
    call kmain
.halt:
    cli
    hlt
    jmp .halt

section .bss align=16
boot_stack_bottom:
    resb 32768
boot_stack_top:

section .note.GNU-stack noalloc noexec nowrite progbits
