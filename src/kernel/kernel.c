#include <kmem.h>
#include <multiboot.h>
#include <acpi.h>
#include <pci.h>
#include <gdt.h>
#include <interrupts.h>
#include <drivers/nvme.h>

__attribute__ ((noreturn)) 
void panic(const char *msg)
{
	(void)msg;
	for(;;)
		;
}


void kernel_main(uint32_t magic, multiboot_info *mb_info) 
{
	if(magic != MULTIBOOT_MAGIC)
		return;
	
	multiboot_framebuffer_tag *fb_tag = NULL;
	multiboot_mmap_tag *mmap_tag = NULL;
	multiboot_acpi_old_tag *acpio_tag = NULL;
	multiboot_acpi_new_tag *acpin_tag = NULL;
	multiboot_tag *tag = &mb_info->tags[0];
	while(tag->type != 0) {
		switch((MultibootTagTypes)tag->type) {
			case MBTag_MemoryMap:
				mmap_tag = (multiboot_mmap_tag *)tag;
				break;
			case MBTag_Framebuffer:
				fb_tag = (multiboot_framebuffer_tag *)tag;
				break;
			case MBTag_ACPIold:
				acpio_tag = (multiboot_acpi_old_tag *)tag;
				break;
			case MBTag_ACPInew:
				acpin_tag = (multiboot_acpi_new_tag *)tag;
				break;
			default:
			break;
		}
		tag = (multiboot_tag *)(ALIGN_UP((uintptr_t)tag + tag->size, 8));
	}
	(void)acpin_tag;

	if(!fb_tag || !mmap_tag || !acpio_tag)
		panic("Failed to find all tags!");

	kmem_init(mmap_tag);

	// @TODO: Task Segment
	size_t gdt_seg_count = 5;
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

	kgdt_set((uintptr_t)seg, gdt_seg_count * sizeof(GDT_Segment) - 1);

	kint_setup_interrupts(&acpio_tag->rsdp);

	if(!rsdp_check_header(&acpio_tag->rsdp))
		panic("Invalid RSDP header!");

	PCIe *pci = pcie_init(&acpio_tag->rsdp);
	if(!pci)
		panic("Failed to init PCIe!");

	NVMeDevice nvme;
	int nvme_res = nvme_init(pci, &nvme);
	if(nvme_res != 0)
		panic("Failed to init NVMe!");

	u8 *nvme_data = NULL;
	uintptr_t nvme_buf = 0;
	if(dma_map(PAGE_SIZE, (void **)&nvme_data, &nvme_buf)) {
		int block_count = PAGE_SIZE / nvme.ns_infos[0].block_size;
		int lba = 0;
		for(int i = 0; i < 16; ++i)
		{
			memset(nvme_data, 0xAC, PAGE_SIZE);
			nvme_write(&nvme, 1, lba, block_count, nvme_buf);
			memset(nvme_data, 0x0, PAGE_SIZE);
			nvme_read(&nvme, 1, lba, block_count, nvme_buf);
			lba += block_count;
		}
	}


	u32 *color = kmem_map(sizeof(u32), PAGE_FLAG_RW);
	if(!color)
		panic("Failed to map kernel memory!");

	*color = 0x0000FFFF;

	// @Note: Maybe it should be MMIO, but not using cache on framebuffer writes
	// sounds very bad for performance, so maybe just find a way to flush it
	// Vasko - 22/07/2026
	uint32_t *framebuffer = kmem_map_phy_addr(fb_tag->framebuffer_addr, fb_tag->framebuffer_width * fb_tag->framebuffer_height * 4, PAGE_FLAG_RW);
	for(;;) {
		uint32_t *buffer = framebuffer;
		for(u32 y = 0; y < fb_tag->framebuffer_height/2; ++y) {
			for(u32 x = 0; x < fb_tag->framebuffer_width; ++x) {
				if (y % 2 == 0 || y % 3 == 0)
					buffer[x] = *color;
			}
			buffer += fb_tag->framebuffer_pitch/4;
		}
	}
}

