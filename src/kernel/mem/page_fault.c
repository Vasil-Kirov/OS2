#include "page_fault.h"
#include <kprintf.h>
#include <interrupts.h>

typedef struct {
	uintptr_t from;
	uintptr_t to;
} FaultFix;

extern const FaultFix __ex_table_start[];
extern const FaultFix __ex_table_end[];

static uintptr_t calc_fix_addr(const uintptr_t *p) { return (uintptr_t)p + *p; }

extern void exception_handler();

void handle_page_fault(InterruptFrame *f, u32 cr2)
{
	if((f->cs & 3) != 0) { 
		kprintf("User-Mode page fault! CR2: %d, ERR: %d", cr2, f->err_code);
		exception_handler();
	}

	for (const FaultFix *fix = __ex_table_start; fix < __ex_table_end; ++fix) {
		if (f->eip == calc_fix_addr(&fix->from))  {
			f->eip = calc_fix_addr(&fix->to);
		}
	}
}

