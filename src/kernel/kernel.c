#include <kmem.h>
#include <multiboot.h>
#include <acpi.h>

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

	if(!fb_tag || !mmap_tag || !acpio_tag)
		panic("Failed to find all tags!");

	(void)acpin_tag;
	if(!check_rsdp_header(&acpio_tag->rsdp))
		panic("Invalid RSDP header!");


	kmem_init(mmap_tag);
	u32 *color = kmem_map(sizeof(u32));
	if(!color)
		panic("Failed to map kernel memory!");

	*color = 0x0000FFFF;

	uint32_t *framebuffer = kmem_map_phy_addr(fb_tag->framebuffer_addr, fb_tag->framebuffer_width * fb_tag->framebuffer_height * 4, 0x3);
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

