
; Declare constants for the multiboot header.
MBALIGN  equ  1 << 0            ; align loaded modules on page boundaries
MEMINFO  equ  1 << 1            ; provide memory map
VIDINFO  equ  1 << 2
MBFLAGS  equ  MBALIGN | MEMINFO | VIDINFO ; this is the Multiboot 'flag' field
MAGIC    equ  0x1BADB002        ; 'magic number' lets bootloader find the header
CHECKSUM equ -(MAGIC + MBFLAGS)   ; checksum of above, to prove we are multiboot

KERNEL_OFFSET equ 0xC0000000

%define phys_addr(a) (a-KERNEL_OFFSET)

section .multiboot.data alloc write
align 4
	dd MAGIC
	dd MBFLAGS
	dd CHECKSUM
	dd 0 ; Load address nonsense
	dd 0 ; Load address nonsense
	dd 0 ; Load address nonsense
	dd 0 ; Load address nonsense
	dd 0 ; Load address nonsense
	dd 0     ; Linear graphics mode
	dd 1600  ; Width
	dd 900   ; Height
	dd 32    ; bits per pixel

section .bootstrap_stack alloc write nobits
stack_bottom:
	resb 0x10000
stack_top:

extern kernel_main
extern _kernel_start
extern _kernel_end

section .multiboot.text
global _start:function (_start.end - _start)
_start:
	mov [phys_addr(mboot_ebx)], ebx
	mov [phys_addr(mboot_eax)], eax

	mov edi, phys_addr(boot_page_table) ; Page Table address
	mov esi, 0 ; Address to map
	mov ecx, 1024 ; Iteration counter

l1:
	mov edx, esi
	or edx, 0b11 ; Present, R/W
	mov [edi], edx

	add edi, 4 ; Next Table Entry
	add esi, 0x1000 ; Next page
	dec ecx
	jnz l1

	mov ecx, phys_addr(boot_page_table) + 0x3
	mov [phys_addr(paging_directory)],           ecx
	mov [phys_addr(paging_directory) + 768 * 4], ecx

	mov ecx, phys_addr(paging_directory)
	mov cr3, ecx

	mov ecx, cr0
	or ecx, (1 << 31) | (1 << 16) ; Enable paging and write protect
	mov cr0, ecx


	lea ecx, higher_kernel
	jmp ecx
_start.end:

global panic
section .text

higher_kernel:
	mov esp, stack_top
	mov ecx, 0
	mov [phys_addr(paging_directory)], ecx ; Unmap identity mapped kernel

	mov ebx, [mboot_ebx]
	mov eax, [mboot_eax]

	add ebx, KERNEL_OFFSET ; Set pointer to virtual memory

	push ebx
	push eax
	call kernel_main

	jmp $
	
panic:
	jmp $


global paging_directory
section .bss
align 0x1000

paging_directory:
	resb 1024 * 4

boot_page_table:
	resb 1024 * 4

mboot_saved_info:
    mboot_eax: resd 1
    mboot_ebx: resd 1

	
