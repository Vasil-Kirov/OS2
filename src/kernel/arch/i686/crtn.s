section .init
    ; gcc crtend.o .init section
    pop ebp
    ret


section .fini
    ; gcc crtend.o .fini section
    pop ebp
    ret
