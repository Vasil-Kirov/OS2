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

	kmem_init_main_kernel_tables();
	physical_mmap = kmem_map_phy_addr((void *)mb_info->mmap_addr, mb_info->mmap_length * sizeof(multiboot_memory_map_t), 0x3);
	physical_mmap_len = mb_info->mmap_length;

	uint32_t *framebuffer = kmem_map_phy_addr((uint32_t*)mb_info->framebuffer_addr, mb_info->framebuffer_width * mb_info->framebuffer_height * 4, 0x3);
	for(;;)
	{
		uint32_t *buffer = framebuffer;
		for(int y = 0; y < mb_info->framebuffer_height/2; ++y)
		{
			for(int x = 0; x < mb_info->framebuffer_width; ++x)
			{
				if (y % 2 == 0 || y % 3 == 0)
					buffer[x] = 0xFFFF00FF;
			}
			buffer += mb_info->framebuffer_pitch/4;
		}
	}
}

