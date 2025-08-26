global _start, _kmain
extern kmain, start_ctors, end_ctors, start_dtors, end_dtors

%define MULTIBOOT_HEADER_MAGIC	0x1BADB002
%define MULTIBOOT_HEADER_FLAGS	0x00000003
%define CHECKSUM -(MULTIBOOT_HEADER_MAGIC + MULTIBOOT_HEADER_FLAGS)

; Entry point
_start:
	jmp start

; Multiboot header
align 4

multiboot_header:
dd MULTIBOOT_HEADER_MAGIC
dd MULTIBOOT_HEADER_FLAGS
dd CHECKSUM

start:
	; Write "BOOT" to VGA memory immediately
	mov edi, 0xB8000
	mov eax, 0x07420742  ; 'B' with attribute 0x07, twice
	mov [edi], eax
	mov eax, 0x074F074F  ; 'O' with attribute 0x07, twice  
	mov [edi+4], eax
	mov eax, 0x07540754  ; 'T' with attribute 0x07, twice
	mov [edi+8], eax
	
	push ebx

static_ctors_loop:
	mov ebx, start_ctors
	jmp .test
.body:
	call [ebx]
	add ebx,4
.test:
	cmp ebx, end_ctors
	jb .body
	
	; Write "MAIN" to VGA memory before calling kmain
	mov edi, 0xB8000
	add edi, 160  ; Second line
	mov eax, 0x074D074D  ; 'M' with attribute 0x07, twice
	mov [edi], eax
	mov eax, 0x07410741  ; 'A' with attribute 0x07, twice
	mov [edi+4], eax
	mov eax, 0x074E074E  ; 'I' and 'N'
	mov [edi+8], eax
	
	; Call kernel
	call kmain

static_dtors_loop:
	mov ebx, start_dtors
	jmp .test
.body:
	call [ebx]
	add ebx,4
.test:
	cmp ebx, end_dtors
	jb .body
	
	cli ; stop interrupts
	hlt ; halt cpu
