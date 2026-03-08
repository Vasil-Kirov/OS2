

global in32
global out32

section .text

; cx = port
in32:
	mov dx, cx
	in eax, dx
	ret

; cx = port
; edx = dword
out32:
	mov eax, edx
	mov dx, cx
	out dx, eax
	ret


