#include "gdt.h"
#include <kmem.h>

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
	__asm__ volatile("LGDT %0"
		:
		:"m"(gdtr));
}


tss_entry tss;

void kgdt_setup()
{
    memset(&tss, 0, sizeof(tss));
    tss.ss0 = KERNEL_DATA_SEGMENT << 3;
    tss.iomap_base = sizeof(tss);     // no I/O bitmap

	size_t gdt_seg_count = 6;
	GDT_Segment *seg = kmem_map(sizeof(GDT_Segment) * gdt_seg_count, PAGE_FLAG_MMIO);
	if(!seg)
		panic("Failed to allocate memory for GDT segments!");

	// NULL segment
	seg[0] = kgdt_make_segment(0, 0, 0, 0);
	// Kernel Code
	seg[1] = kgdt_make_segment(0, 0xFFFFF, 0x9A, 0xC);
	// Kernel Data
	seg[2] = kgdt_make_segment(0, 0xFFFFF, 0x92, 0xC);
	// User Code
	seg[3] = kgdt_make_segment(0, 0xFFFFF, 0xFA, 0xC);
	// User Data
	seg[4] = kgdt_make_segment(0, 0xFFFFF, 0xF2, 0xC);
	// TSS
	seg[5] = kgdt_make_segment((u32)&tss, sizeof(tss)-1, 0x89, 0x0);

	kgdt_set((uintptr_t)seg, gdt_seg_count * sizeof(GDT_Segment) - 1);

    __asm__ volatile("ltr %0" :: "r"((uint16_t)0x2B));  // (5 << 3) | 3
}

