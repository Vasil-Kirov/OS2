#include "interrupts.h"

#include <kmem.h>
#include <io.h>
#include <kcpuid.h>
#include <errno.h>
#include <msr.h>
#include <acpi.h>

#define IA32_APIC_BASE_MSR 0x1B
#define IA32_APIC_BASE_MSR_BSP 0x100 // Processor is a BSP
#define IA32_APIC_BASE_MSR_ENABLE 0x800

__attribute__((aligned(0x10))) 
static IDT_Entry idt[INT_COUNT];
static IDTR idtr;

static u32 volatile *ioapic = NULL;
static u8 volatile *lapic = NULL;

extern void *isr_stub_table[];

void kint_set_idt(int idx, void *ptr, u8 attrs)
{
	idt[idx] = (IDT_Entry) {
		.offset_low = ((u32)ptr) & 0xFFFF,
		.offset_high = ((u32)ptr) >> 16,
		.seg_selector = KERNEL_CODE_SEGMENT << 3,
		.zero = 0,
		.attributes = attrs,
	};
}

u32 kint_ioapic_read(u32 reg)
{
	ioapic[0] = reg & 0xFF;
	return ioapic[4];
}

void kint_ioapic_write(u32 reg, u32 value)
{
	ioapic[0] = reg & 0xFF;
	ioapic[4] = value;
}

// Still need to do this because PIC can fire random interrupts
void kint_remap_legacy_pic(int off1, int off2)
{
	out32(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);  // starts the initialization sequence (in cascade mode)
	out32(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
	out32(PIC1_DATA, off1);                 // ICW2: Master PIC vector offset
	out32(PIC2_DATA, off2);                 // ICW2: Slave PIC vector offset
	out32(PIC1_DATA, 1 << CASCADE_IRQ);        // ICW3: tell Master PIC that there is a slave PIC at IRQ2
	out32(PIC2_DATA, 2);                       // ICW3: tell Slave PIC its cascade identity (0000 0010)

	out32(PIC1_DATA, ICW4_8086);               // ICW4: have the PICs use 8086 mode (and not 8080 mode)
	out32(PIC2_DATA, ICW4_8086);
}

void kint_disable_legacy_pic()
{
	out8(PIC1_DATA, 0xff);
	out8(PIC2_DATA, 0xff);
}

static bool kint_check_apic()
{
    unsigned int eax, unused, edx;
    __get_cpuid(1, &eax, &unused, &unused, &edx);
    return (edx & CPUID_FEAT_EDX_APIC) != 0;
}

void cpu_set_apic_base(uintptr_t apic) {
   uint32_t edx = 0;
   uint32_t eax = (apic & 0xfffff0000) | IA32_APIC_BASE_MSR_ENABLE;

   cpu_set_msr(IA32_APIC_BASE_MSR, eax, edx);
}

uintptr_t cpu_get_apic_base() {
   uint32_t eax, edx;
   cpu_get_msr(IA32_APIC_BASE_MSR, &eax, &edx);

   return (eax & 0xfffff000);
}

int kint_setup_interrupts(RSDP *rsdp)
{
	int ret;
	kint_disable_interrupts();

	idtr.base = (uintptr_t)idt;
	idtr.limit = sizeof(idt) - 1;
	for(int i = 0; i < INT_COUNT; ++i)
		kint_set_idt(i, isr_stub_table[i], 0x8E);

	kint_remap_legacy_pic(32, 40);
	kint_disable_legacy_pic();
	

	if(!kint_check_apic())
		return -ENODEV;

	uintptr_t apic_base_phy = cpu_get_apic_base();
	cpu_set_apic_base(apic_base_phy);
	lapic = kmem_map_phy_addr(apic_base_phy, 0x1000, PAGE_FLAG_RW);
	if(!lapic)
		return -ENOMEM;

	u32 table_length;
	uintptr_t phy_apic_table = rsdp_find_table(rsdp, "APIC", &table_length);
	if(!phy_apic_table) {
		ret = -ENOENT;
		goto err_unmap_local_apic;
	}

	MADT *madt = kmem_map_phy_addr(phy_apic_table, table_length, PAGE_FLAG_RW);
	if(madt->local_apic_addr != apic_base_phy) {
		// @TODO: Warning?
	}
	
	uintptr_t io_apic_phy = 0;
	u8 *entry_ptr = (u8 *)madt + sizeof(MADT);
	u8 *entry_end = (u8 *)madt + table_length;

	while(entry_ptr < entry_end) {
		MADT_EntryHeader *entry = (MADT_EntryHeader *)entry_ptr;

		switch(entry->entry_type) {
		case 0: // Local APIC, maybe take a look?
			break;

		case 1: // I/O APIC
			io_apic_phy = ((MADT_IOApic *)entry)->address;
			break;

		default: break;
		}

		entry_ptr += entry->record_len;
	}

	if(io_apic_phy == 0) {
		ret = -ENOENT;
		goto err_unmap_madt;
	}

	size_t io_size = 0x40;
	ioapic = kmem_map_phy_addr(io_apic_phy, 0x1000, PAGE_FLAG_RW);
	if(!ioapic) {
		ret = -ENOMEM;
		goto err_unmap_madt;
	}
	kmem_unmap_raw(madt, 0x1000);


	write32(lapic+0xF0, read32(lapic+0xF0) | 0x1FF); // Enable Suprious Interrupts

	__asm__ volatile ("lidt %0" : : "m"(idtr)); // load the new IDT
	kint_enable_interrupts();

	return 0;

err_unmap_madt:
		kmem_unmap_raw(madt, 0x1000);

err_unmap_local_apic:
		kmem_unmap_raw((void *)lapic, 0x1000);
		lapic = NULL;
		return ret;
}

__attribute__((noreturn))
void exception_handler() {
	__asm__ volatile ("cli; hlt"); // Completely hangs the computer
	for(;;)
		;
}

void kint_disable_interrupts()
{
	__asm__ volatile ("cli");
}

void kint_enable_interrupts()
{
	__asm__ volatile ("sti");
}

