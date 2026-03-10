
#ifndef _INT_H
#define _INT_H

#include <kcommon.h>

typedef struct {
	u16 offset_low;
	u16 seg_selector;
	u8 zero;
	u8 attributes;
	u16 offset_high;
} __attribute__((packed)) IDT_Entry;

typedef struct {
	u16 limit;
	u32 base;
} __attribute__((packed)) IDTR;


#define INT_COUNT (256)
#define KERNEL_CODE_SEGMENT (1)

int kint_setup_idt();

#endif

