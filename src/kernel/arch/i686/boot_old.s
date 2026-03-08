; Declare constants for the multiboot header.
MBALIGN  equ  1 << 0            ; align loaded modules on page boundaries
MEMINFO  equ  1 << 1            ; provide memory map
VIDINFO  equ  1 << 2
MBFLAGS  equ  MBALIGN | MEMINFO | VIDINFO ; this is the Multiboot 'flag' field
MAGIC    equ  0x1BADB002        ; 'magic number' lets bootloader find the header
CHECKSUM equ -(MAGIC + MBFLAGS)   ; checksum of above, to prove we are multiboot


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

extern kernel_main
extern _kernel_start
extern _kernel_end

section .multiboot.text
global _start:function (_start.end - _start)
_start:
	cli

	mov ecx, [ebx + 88]
	and ecx, 0xFFFFF000
	or ecx, 0x3
	mov [boot_page_table1 - 0xC0000000 + 1023 * 4], ecx

	; Load the first page directory in CR3
	mov ecx, boot_page_directory - 0xC0000000
	mov cr3, ecx
	
	; Enable paging
	mov ecx, cr0
	or ecx, 0x80000000
	mov cr0, ecx

	lea ecx, high_kernel
	jmp ecx
_start.end:

section .text

high_kernel:

	; To set up a stack, we set the esp register to point to the top of our
	; stack (as it grows downwards on x86 systems). This is necessarily done
	; in assembly as languages such as C cannot function without a stack.
	mov esp, stack_top


	; This is a good place to initialize crucial processor state before the
	; high-level kernel is entered. It's best to minimize the early
	; environment where crucial features are offline. Note that the
	; processor is not fully initialized yet: Features such as floating
	; point instructions and instruction set extensions are not initialized
	; yet. The GDT should be loaded here. Paging should be enabled here.
	; C++ features such as global constructors and exceptions will require
	; runtime support to work as well.

	; Enter the high-level kernel. The ABI requires the stack is 16-byte
	; aligned at the time of the call instruction (which afterwards pushes
	; the return pointer of size 4 bytes). The stack was originally 16-byte
	; aligned above and we've since pushed a multiple of 16 bytes to the
	; stack since (pushed 0 bytes so far) and the alignment is thus
	; preserved and the call is well defined.
    ; note, that if you are building on Windows, C functions may have "_" prefix in assembly: _kernel_main

	;mov sys_multiboot_info, ebx

	push ebx
	push eax
	call kernel_main

	; If the system has nothing more to do, put the computer into an
	; infinite loop. To do that:
	; 1) Disable interrupts with cli (clear interrupt enable in eflags).
	;    They are already disabled by the bootloader, so this is not needed.
	;    Mind that you might later enable interrupts and return from
	;    kernel_main (which is sort of nonsensical to do).
	; 2) Wait for the next interrupt to arrive with hlt (halt instruction).
	;    Since they are disabled, this will lock up the computer.
	; 3) Jump to the hlt instruction if it ever wakes up due to a
	;    non-maskable interrupt occurring or due to system management mode.
	cli

.hang:	hlt
	jmp .hang

section .bss
align 0x1000 ; Align on page boundary

stack_bottom:
resb 0x10000 ; Enough memory... hopefully
stack_top:


section .entry.data
align 0x1000
boot_page_table1:
	%assign addr 0
	%rep 1024
		dd addr + 0x3
		%assign addr addr+0x1000
	%endrep

; Mirror first 4 MB
global boot_page_directory
boot_page_directory:
	%rep 1024
		dd (boot_page_table1 - 0xC0000000) + 0x7
	%endrep

