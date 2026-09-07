section .init
global _init

_init:
    push ebp
    mov ebp, esp
    ; gcc crtbegin.o .init section


section .fini
global _fini

_fini:
    push ebp
    mov ebp, esp
    ; gcc crtbegin.o .fini section


