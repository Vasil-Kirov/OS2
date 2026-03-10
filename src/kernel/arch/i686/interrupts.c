#include "interrupts.h"

#include <kmem.h>

__attribute__((aligned(0x10))) 
static IDT_Entry idt[INT_COUNT];
static IDTR idtr;

extern void *isr_stub_table[];

void kint_set_idt(int idx, void *ptr, u8 attrs)
{
	idt[idx] = (IDT_Entry) {
		.offset_low = ((u32)ptr) & 0xFFFF,
		.offset_high = ((u32)ptr) >> 16,
		.seg_selector = KERNEL_CODE_SEGMENT << 3,
		.zero = 0,
		.attributes = attrs,
	};
}

int kint_setup_idt()
{
	idtr.base = (uintptr_t)idt;
	idtr.limit = sizeof(idt) - 1;
	for(int i = 0; i < INT_COUNT; ++i)
		kint_set_idt(i, isr_stub_table[i], 0x8E);

	__asm__ volatile ("lidt %0" : : "m"(idtr)); // load the new IDT
	__asm__ volatile ("sti"); // set the interrupt flag
	
	return 0;
}

__attribute__((noreturn))
void exception_handler() {
    __asm__ volatile ("cli; hlt"); // Completely hangs the computer
    for(;;)
	    ;
}

