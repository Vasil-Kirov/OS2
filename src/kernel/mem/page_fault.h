
#ifndef _PAGE_FAULT_H
#define _PAGE_FAULT_H
#include <interrupts.h>

#define _ASM_EXTABLE(from, to)            \
    ".pushsection .ex_table, \"a\"\n"     \
    ".balign 4\n"                         \
    ".long (" #from ") - .\n"             \
    ".long (" #to ") - .\n"               \
    ".popsection\n"

static_assert(sizeof(uintptr_t) == sizeof(int));

#define __user_read(ptr, label)                   \
({                                                \
    typeof_unqual(*(ptr)) __v;                           \
    __asm__ goto("1: mov %[dst], %[src]\n"        \
             _ASM_EXTABLE(1b, %l[label])          \
             : [dst] "=r" (__v)                   \
             : [src] "m" (*(ptr))                 \
             :                                    \
             : label);                            \
    __v;                                          \
})

#define __user_write(ptr, v, label)               \
    __asm__ goto("1: mov %[dst], %[src]\n"        \
             _ASM_EXTABLE(1b, %l[label])          \
             : [dst] "=m" (*(ptr))                \
             : [src] "r" (v)                      \
             : "memory"                           \
             : label)                             \


void handle_page_fault(InterruptFrame *f, u32 cr2);


#endif

