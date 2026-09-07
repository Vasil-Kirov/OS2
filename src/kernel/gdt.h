
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

typedef struct __attribute__((packed)) {
    u32 prev;
    u32 esp0, ss0;
    u32 esp1, ss1, esp2, ss2;
    u32 cr3, eip, eflags;
    u32 eax, ecx, edx, ebx, esp, ebp, esi, edi;
    u32 es, cs, ss, ds, fs, gs, ldt;
    u16 trap, iomap_base;
	u32 ssp;
} tss_entry;

void kgdt_set(u32 base, u16 limit);
GDT_Segment kgdt_make_segment(u32 base, u32 limit, u8 access, u8 flags);
void kgdt_setup();

#define KERNEL_CODE_SEGMENT (1)
#define KERNEL_DATA_SEGMENT (2)
#define USER_CODE_SEGMENT (3)
#define USER_DATA_SEGMENT (4)

#endif

