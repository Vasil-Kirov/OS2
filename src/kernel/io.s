

global in32
global out8
global out32

section .text

; cx = port
in32:
	mov dx, cx
	in eax, dx
	ret

; cx = port
; dl = byte
out8:
	mov al, dl
	mov dx, cx
	out dx, al
	ret

; cx = port
; edx = dword
out32:
	mov eax, edx
	mov dx, cx
	out dx, eax
	ret


