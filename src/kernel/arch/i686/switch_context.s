

global enter_proc_

USER_CS equ 3 << 3 | 3
USER_DS equ 4 << 3 | 3

section .text


; Arguments
; 0 = entry
; 1 = stack top
; 2 = cr3
enter_proc_:

	mov eax, [esp + 4]
	mov ecx, [esp + 8]
	mov edx, [esp + 12]
	mov cr3, edx

    mov dx, USER_DS
    mov ds, dx
    mov es, dx
    mov fs, dx
    mov gs, dx

	push DWORD USER_DS ; ss
	push ecx           ; esp
	push DWORD 0x202   ; eflags = int enabled, reserved=1
	push DWORD USER_CS ; cs
	push eax           ; eip = entry
	iret



