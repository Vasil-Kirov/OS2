
#include "gdt.h"
#include "proc/proc.h"

Process *current_proc = NULL;

void set_current_proc(Process *next)
{
	current_proc = next;
}

Process *get_current_proc()
{
	return current_proc;
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

void scheduler_tick(InterruptFrame *frame)
{
	Process *proc = get_current_proc();
	if (!proc)
		return;

	proc->frame = frame;
	set_current_proc(proc->next);
}

extern tss_entry tss;
void enter_proc(Process *proc)
{
	tss.esp0 = proc->kernel_stack_top;
	scheduler_add_proc(proc);
	set_current_proc(proc);
	enter_proc_((uintptr_t)proc->entry, proc->stack_top, proc->vm.page_directory_paddr.addr);
}


