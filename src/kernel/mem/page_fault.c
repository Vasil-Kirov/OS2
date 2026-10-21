#include "page_fault.h"
#include <proc/proc.h>
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

bool handle_page_fault(InterruptFrame *f, uintptr_t cr2)
{
	Process *proc = get_current_proc();
	if (proc) {
		VMRegion *region = vm_find_region(&proc->vm, (void *)cr2);
		if (region && (region->mmap_flags & MMAP_NO_ACCESS) == 0 && (f->err_code & 0b1) == 0) {
			// f->err_code bit 0 == 0 -> page was not present
			switch (region->type) {
				case VMArea_AllocOnAccess:
				{
					uintptr_t commit_addr = ALIGN_DOWN(cr2, PAGE_SIZE);
					return vm_commit_region(region, cr2-commit_addr, PAGE_SIZE) != NULL;
				} break;
				default: break;
			}
		}
	}

	if((f->cs & 3) != 0) { 
		kprintf("User-Mode page fault! CR2: %d, ERR: %d", cr2, f->err_code);
		return false;
	}

	for (const FaultFix *fix = __ex_table_start; fix < __ex_table_end; ++fix) {
		if (f->eip == calc_fix_addr(&fix->from))  {
			f->eip = calc_fix_addr(&fix->to);
			return true;
		}
	}
	return false;
}

