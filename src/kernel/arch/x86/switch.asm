global do_switch

section .text

do_switch:
    ; retrieve the address of *current
    mov esi, [esp]       ; load the pointer to the current process structure
    pop eax              ; remove @current from the stack

    ; prepare registers for context switching
    push dword [esi + 4]  ; eax
    push dword [esi + 8]  ; ecx
    push dword [esi + 12] ; edx
    push dword [esi + 16] ; ebx
    push dword [esi + 24] ; ebp
    push dword [esi + 28] ; esi
    push dword [esi + 32] ; edi
    push dword [esi + 48] ; ds
    push dword [esi + 50] ; es
    push dword [esi + 52] ; fs
    push dword [esi + 54] ; gs

    ; disable interrupt mask on the PIC
    mov al, 0x20
    out 0x20, al

    ; load the page table base address
    mov eax, [esi + 56]   ; load cr3 value from process structure
    mov cr3, eax          ; set the new page table

    ; load registers
    pop gs
    pop fs
    pop es
    pop ds
    pop edi
    pop esi
    pop ebp
    pop ebx
    pop edx
    pop ecx
    pop eax

    ; return to the process
    iret
