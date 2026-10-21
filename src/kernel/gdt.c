#include "gdt.h"
#include <kmem.h>

static GDT_Segment *gdt;

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

void kgdt_set_tls(void *base)
{
	// @TODO: THREADING
	gdt[6] = kgdt_make_segment((u32)base, 0xFFFFF, 0xF2, 0xC);
	__asm__ volatile("mov gs, %0" :: "r"((u16)((TLS_SEGMENT << 3) | 3)));
}

void kgdt_setup()
{
    memset(&tss, 0, sizeof(tss));
    tss.ss0 = KERNEL_DATA_SEGMENT << 3;
    tss.iomap_base = sizeof(tss);     // no I/O bitmap

	size_t gdt_seg_count = 7;
	gdt = kmem_map(sizeof(GDT_Segment) * gdt_seg_count, PAGE_FLAG_MMIO);
	if(!gdt)
		panic("Failed to allocate memory for GDT segments!");

	// NULL segment
	gdt[0] = kgdt_make_segment(0, 0, 0, 0);
	// Kernel Code
	gdt[1] = kgdt_make_segment(0, 0xFFFFF, 0x9A, 0xC);
	// Kernel Data
	gdt[2] = kgdt_make_segment(0, 0xFFFFF, 0x92, 0xC);
	// User Code
	gdt[3] = kgdt_make_segment(0, 0xFFFFF, 0xFA, 0xC);
	// User Data
	gdt[4] = kgdt_make_segment(0, 0xFFFFF, 0xF2, 0xC);
	// TSS
	gdt[5] = kgdt_make_segment((u32)&tss, sizeof(tss)-1, 0x89, 0x0);
	// TLS
	gdt[6] = kgdt_make_segment(0, 0xFFFFF, 0xF2, 0xC);

	kgdt_set((uintptr_t)gdt, gdt_seg_count * sizeof(GDT_Segment) - 1);

    __asm__ volatile("ltr %0" :: "r"((uint16_t)0x2B));  // (5 << 3) | 3
}

