
#include "stack_trace.h"
#include <kprintf.h>

u32 walk_stack(u32 ebp, u32 *dst, u32 size);

void trace_stack(u32 ebp)
{
	u32 buf[1024] = {};
	u32 count = walk_stack(ebp, buf, 1024);

	kprintf("Stack: %d", count);
	for (size_t i = 0; i < count; ++i) {
		kprintf("\t%p", buf[i]);
	}

}

