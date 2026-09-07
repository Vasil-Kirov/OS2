
#ifndef _PROC_H
#define _PROC_H

#include <vfs.h>
#include <kcommon.h>
#include <kmem.h>

#define MAX_FDS (64)
typedef struct Process {
	int pid;
	void *entry;
	void *stack;
	uintptr_t stack_top;
	void *kernel_stack;
	uintptr_t kernel_stack_top;
	struct Process *next;
	File *fds[MAX_FDS];
	AddressSpace vm;
} Process;

#define DEFAULT_STACK_SIZE (KB(8))

[[noreturn]]
void enter_proc(Process *proc);

[[noreturn]]
void enter_proc_(uintptr_t entry, uintptr_t stack, uintptr_t cr3) ;

Process *get_current_proc();

#endif

