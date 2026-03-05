.section .init
    /* gcc crtend.o .init section */
    popl %ebp
    ret

.section .fini
    /* gcc crtend.o .fini section */
    popl %ebp
    ret
