
global _start

section .text

_start:
	mov eax, 0
	mov ebx, msg
	int 0x80
	jmp _start



msg db "Hello World!", 0x0a, 0x0

