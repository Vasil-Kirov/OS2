
#ifndef _PROC_H
#define _PROC_H

#include <interrupts.h>
#include <kcommon.h>
#include <kmem.h>

#define MAX_FDS (64)

struct File;

typedef struct {
	u32 uid;
	u32 gid;
} Credentials;

typedef struct {
	void __user *template_ptr;
	u32 file_sz;
	u32 mem_sz;
	u32 align;
} ThreadLocaLStorageInfo;

typedef struct Process {
	int pid;
	int parent;
	void *entry;
	void *stack;
	uintptr_t stack_top;
	void *kernel_stack;
	uintptr_t kernel_stack_top;
	struct Process *prev;
	struct Process *next;
	struct File *fds[MAX_FDS];
	ThreadLocaLStorageInfo tls;
	void __user *tp;
	InterruptFrame *frame;
	AddressSpace vm;
} Process;


#define DEFAULT_STACK_SIZE (KB(32))

[[noreturn]]
void enter_proc(Process *proc);

[[noreturn]]
void enter_proc_(uintptr_t entry, uintptr_t stack, uintptr_t cr3) ;

void scheduler_tick(InterruptFrame *frame);
void scheduler_add_proc(Process *proc);
void scheduler_remove_current();
Process *get_current_proc();

#endif

