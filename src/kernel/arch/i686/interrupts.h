
#ifndef _INT_H
#define _INT_H

#include <kcommon.h>
#include <acpi.h>

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

#define PIC1		0x20		/* IO base address for master PIC */
#define PIC2		0xA0		/* IO base address for slave PIC */
#define PIC1_COMMAND	PIC1
#define PIC1_DATA	(PIC1+1)
#define PIC2_COMMAND	PIC2
#define PIC2_DATA	(PIC2+1)

#define ICW1_ICW4	0x01		/* Indicates that ICW4 will be present */
#define ICW1_SINGLE	0x02		/* Single (cascade) mode */
#define ICW1_INTERVAL4	0x04		/* Call address interval 4 (8) */
#define ICW1_LEVEL	0x08		/* Level triggered (edge) mode */
#define ICW1_INIT	0x10		/* Initialization - required! */

#define ICW4_8086	0x01		/* 8086/88 (MCS-80/85) mode */
#define ICW4_AUTO	0x02		/* Auto (normal) EOI */
#define ICW4_BUF_SLAVE	0x08		/* Buffered mode/slave */
#define ICW4_BUF_MASTER	0x0C		/* Buffered mode/master */
#define ICW4_SFNM	0x10		/* Special fully nested (not) */

#define CASCADE_IRQ 2


int kint_setup_interrupts(RSDP *rsdp);
void kint_disable_interrupts();
void kint_enable_interrupts();

typedef struct {
	u8 entry_type;
	u8 record_len;
} __attribute__((packed)) MADT_EntryHeader;

typedef struct {
	MADT_EntryHeader h;
	u8 id;
	u8 zero;
	u32 address;
	u32 global_system_interrupt_base;
} MADT_IOApic;

typedef struct {
	ACPISDTHeader h;
	u32 local_apic_addr;
	u32 flags;
} MADT;


#endif // _INT_H

