#include "interrupts.h"

#include <kprintf.h>
#include <kmem.h>
#include <io.h>
#include <kcpuid.h>
#include <errno.h>
#include <msr.h>
#include <acpi.h>
#include <gdt.h>
#include <syscall/syscall.h>
#include <mem/page_fault.h>
#include <proc/proc.h>

#define IA32_APIC_BASE_MSR 0x1B
#define IA32_APIC_BASE_MSR_BSP 0x100 // Processor is a BSP
#define IA32_APIC_BASE_MSR_ENABLE 0x800

#define LAPIC_REG_ID  0x020 // R/W
#define LAPIC_REG_VER 0x030 // R
#define LAPIC_REG_Task_Priority 0x080 // R/W
#define LAPIC_REG_APR 0x090 // R
#define LAPIC_REG_PPR 0x0A0 // R
#define LAPIC_REG_EOI 0x0B0 // W
#define LAPIC_REG_RRD 0x0C0 // R
#define LAPIC_REG_LDR 0x0D0 // R/W
#define LAPIC_REG_DFR 0x0E0 // R/W
#define LAPIC_REG_SPURIOUS_INT_VEC 0x0F0 // R/W
#define LAPIC_REG_InService 0x100 // R
#define LAPIC_REG_TriggerMode 0x180 // R
#define LAPIC_REG_IntReq 0x200 // R
#define LAPIC_REG_ErrStatus 0x280 // R
#define LAPIC_REG_CMCI 0x2F0 // R/W
#define LAPIC_REG_IntCmd 0x300 // R/W
#define LAPIC_REG_LVT_Timer 0x320 // R/W
#define LAPIC_REG_LVT_Thermal 0x330 // R/W
#define LAPIC_REG_LVT_Perf 0x340 // R/W
#define LAPIC_REG_LVT_INT0 0x350 // R/W
#define LAPIC_REG_LVT_INT1 0x360 // R/W
#define LAPIC_REG_LVT_Err 0x370 // R/W
#define LAPIC_REG_Timer_Initial_Count 0x380 // R/W
#define LAPIC_REG_Timer_Count 0x390 // R
#define LAPIC_REG_Timer_Div_Config 0x3E0 // R/W

__attribute__((aligned(0x10))) 
static IDT_Entry idt[INT_COUNT];
static IDTR idtr;

static_assert(sizeof(IDTR) == 6);
static_assert(sizeof(IDT_Entry) == 8);

static u32 volatile *ioapic = NULL;
static u8 volatile *lapic = NULL;

extern void *isr_stub_table[];

u32 lapic_ticks_per_sec;

#define PIT_DATA2 0x42
#define PIT_CMD   0x43
#define PIT_GATE2 0x61

u32 kint_lapic_compute_ticks_per_sec()
{
	out8(PIT_GATE2, in8(PIT_GATE2) & ~0x03);

	out8(PIT_CMD, 0xB0);

    // 10 ms = 1193182 * 0.01 = 11932 ticks
	out8(PIT_DATA2, 11932 & 0xFF);
	out8(PIT_DATA2, 11932 >> 8);

	write32(lapic+LAPIC_REG_Timer_Div_Config, (read32(lapic+LAPIC_REG_Timer_Div_Config) & ~0b1111) | 0b0011); // /= 16
	write32(lapic+LAPIC_REG_LVT_Timer, 1 << 16); // masked interrupts, one-shot mode
	write32(lapic+LAPIC_REG_Timer_Initial_Count, ~(u32)0); // Counts down from U32_MAX

	// Raise gate, start counting
    out8(PIT_GATE2, (in8(PIT_GATE2) & ~0x02) | 0x01);

	// Wait
    while (!(in8(PIT_GATE2) & 0x20))
		;

	u32 remain = read32(lapic+LAPIC_REG_Timer_Count);
	write32(lapic+LAPIC_REG_Timer_Count, 0);

	return ((~(u32)0) - remain) * 100;
}

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
	out8(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);  // starts the initialization sequence (in cascade mode)
	out8(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
	out8(PIC1_DATA, off1);                 // ICW2: Master PIC vector offset
	out8(PIC2_DATA, off2);                 // ICW2: Slave PIC vector offset
	out8(PIC1_DATA, 1 << CASCADE_IRQ);        // ICW3: tell Master PIC that there is a slave PIC at IRQ2
	out8(PIC2_DATA, 2);                       // ICW3: tell Slave PIC its cascade identity (0000 0010)

	out8(PIC1_DATA, ICW4_8086);               // ICW4: have the PICs use 8086 mode (and not 8080 mode)
	out8(PIC2_DATA, ICW4_8086);
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
   uint32_t eax = (apic & 0xfffff000) | IA32_APIC_BASE_MSR_ENABLE;

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
	for(int i = 0; i < INT_COUNT; ++i) {
		if (i == 128) {
			//                        Syscall, Callable from user-mode
			kint_set_idt(i, isr_stub_table[i], 0xEE);
		} else {
			kint_set_idt(i, isr_stub_table[i], 0x8E);
		}
	}

	kint_remap_legacy_pic(32, 40);
	kint_disable_legacy_pic();
	

	if(!kint_check_apic())
		return -ENODEV;

	uintptr_t apic_base_phy = cpu_get_apic_base();
	cpu_set_apic_base(apic_base_phy);
	lapic = kmem_map_phy_addr(apic_base_phy, 0x1000, PAGE_FLAG_MMIO);
	if(!lapic)
		return -ENOMEM;

	u32 table_length;
	uintptr_t phy_apic_table = rsdp_find_table(rsdp, "APIC", &table_length);
	if(!phy_apic_table) {
		ret = -ENOENT;
		goto err_unmap_local_apic;
	}

	MADT *madt = kmem_map_phy_addr(phy_apic_table, table_length, PAGE_FLAG_MMIO);
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

	//size_t io_size = 0x40;
	ioapic = kmem_map_phy_addr(io_apic_phy, 0x1000, PAGE_FLAG_MMIO);
	if(!ioapic) {
		ret = -ENOMEM;
		goto err_unmap_madt;
	}
	kmem_unmap_raw(madt, 0x1000);


	write32(lapic+LAPIC_REG_SPURIOUS_INT_VEC, read32(lapic+LAPIC_REG_SPURIOUS_INT_VEC) | 0x1FF); // Enable Suprious Interrupts
	write32(lapic+LAPIC_REG_Task_Priority, read32(lapic+LAPIC_REG_Task_Priority) & ~0xFF); // Clear task priority

	__asm__ volatile ("lidt %0" : : "m"(idtr)); // load the new IDT

	lapic_ticks_per_sec = kint_lapic_compute_ticks_per_sec();

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

#define TIMER_VECTOR_N (0x20)

void kint_start_timer()
{
	kint_disable_interrupts();

	write32(lapic+LAPIC_REG_Timer_Div_Config, (read32(lapic+LAPIC_REG_Timer_Div_Config) & ~0b1111) | 0b0011); // /= 16
	write32(lapic+LAPIC_REG_LVT_Timer, TIMER_VECTOR_N | (1 << 17)); // Interrupt vector, periodic mode
	write32(lapic+LAPIC_REG_Timer_Initial_Count, lapic_ticks_per_sec / 200); // 5 ms

	kint_enable_interrupts();
}

extern tss_entry tss;
InterruptFrame *interrupt_handler(InterruptFrame *f)
{
	if (f->vector < 32) {
		u32 cr2;
		__asm__ volatile (
				"mov %0, cr2"
				: "=r"(cr2)
				);
		if (f->vector == 14 /* PAGE FAULT */) {
			if (handle_page_fault(f, cr2))
				return f;
		}
		kprintf("CPU Exception %d: %d | cr2: %d", f->vector, f->err_code, cr2);
		exception_handler();
		return f; // no EOI
	}
	switch (f->vector)
	{
		case 0x20: // timer interrupt
		{
			scheduler_tick(f);
			Process *proc = get_current_proc();
			//kprintf("Timer interrupt fired, got proc: %p", proc);
			if (proc) {
				f = proc->frame;
				tss.esp0 = proc->kernel_stack_top;
				u32 cr3 = proc->vm.page_directory_paddr.addr;
				__asm__ volatile (
						"mov cr3, %0"
						:: "r"(cr3) : "memory"
						);
			}
		} break;
		case 0x80: // syscall
		{
			f->eax = handle_syscall(f->eax, f->ebx, f->ecx, f->edx, f->esi, f->edi);
			return f; // no EOI
		} break;
		default:
		break;
	}

	write32(lapic + LAPIC_REG_EOI, 0);
	return f;
}

void kint_disable_interrupts()
{
	__asm__ volatile ("cli");
}

void kint_enable_interrupts()
{
	__asm__ volatile ("sti");
}

void apic_start_timer() {
	write32(lapic+LAPIC_REG_Timer_Div_Config, 0x3);
}

