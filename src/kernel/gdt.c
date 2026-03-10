#include "gdt.h"



GDT_Segment kgdt_make_segment(u32 base, u32 limit, u8 access, u8 flags)
{
	GDT_Segment segment = {
		.base_low = base & 0xFFFF,
		.base_mid = (base >> 16) & 0xFF,
		.base_high = (base >> 24) & 0xFF,
		.access = access,
		.limit = limit & 0xFFFF,
		.flags = ((limit >> 16) & 0xF) | (flags << 4),
	};
	return segment;
}

void kgdt_set(u32 base, u16 limit) {
	GDTR gdtr = { .limit = limit, .base = base };
	__asm__("LGDT %0"
		:
		:"m"(gdtr));
}

