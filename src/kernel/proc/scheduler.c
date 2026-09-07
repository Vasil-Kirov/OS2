
#include "gdt.h"
#include "proc/proc.h"

Process *current_proc = NULL;

Process *get_current_proc()
{
	return current_proc;
}

void scheduler_init()
{
}

void scheduler_add_proc(Process *proc)
{
	if (current_proc == NULL) {
		current_proc = proc;
		current_proc->next = current_proc;
	} else {
		proc->next = current_proc->next;
		current_proc->next = proc;
	}
}

void schedule()
{

}

extern tss_entry tss;
void enter_proc(Process *proc)
{
	tss.esp0 = proc->kernel_stack_top;
	current_proc = proc;
	enter_proc_((uintptr_t)proc->entry, proc->stack_top, proc->vm.page_directory_paddr.addr);
}


