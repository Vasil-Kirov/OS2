
#ifndef _GDT_H
#define _GDT_H

#include <kcommon.h>

typedef struct  __attribute__((packed)) {
	u16 limit;
	u16 base_low;
	u8 base_mid;
	u8 access;
	u8 flags; // | limit_high
	u8 base_high;
} GDT_Segment;

typedef struct __attribute__((packed)) {
	u16 limit;
	u32 base;
} GDTR;

void kgdt_set(u32 base, u16 limit);
GDT_Segment kgdt_make_segment(u32 base, u32 limit, u8 access, u8 flags);

#endif

