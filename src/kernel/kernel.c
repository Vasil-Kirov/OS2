#include "kmem.h"
#include "multiboot.h"

void kernel_main(uint32_t magic, multiboot_info *mb_info) 
{
	if(magic != 0x2BADB002)
	{
		return;
	}
	
	if((mb_info->flags & (1 << 12)) == 0 || mb_info->framebuffer_bpp != 32)
	{
		return;
	}

	kernel_page_virtual_address = kmem_add_page_table((uint32_t)kernel_page_table);

	uint32_t *framebuffer = kmem_map((uint32_t*)mb_info->framebuffer_addr, mb_info->framebuffer_width * mb_info->framebuffer_height * 4);
	for(;;)
	{
		uint32_t *buffer = framebuffer;
		for(int y = 0; y < mb_info->framebuffer_height; ++y)
		{
			for(int x = 0; x < mb_info->framebuffer_width; ++x)
			{
				if (y%2==0)
					buffer[x] = 0xFFFF00FF;
			}
			buffer += mb_info->framebuffer_pitch/4;
		}
	}
}

