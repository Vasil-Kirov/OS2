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

	kmem_init(mb_info);
	u32 *color = kmem_map(sizeof(u32));
	if(!color)
		panic("Failed to map kernel memory!");

	*color = 0x0000FFFF;


	uint32_t *framebuffer = kmem_map_phy_addr(mb_info->framebuffer_addr, mb_info->framebuffer_width * mb_info->framebuffer_height * 4, 0x3);
	for(;;)
	{
		uint32_t *buffer = framebuffer;
		for(u32 y = 0; y < mb_info->framebuffer_height/2; ++y)
		{
			for(u32 x = 0; x < mb_info->framebuffer_width; ++x)
			{
				if (y % 2 == 0 || y % 3 == 0)
					buffer[x] = *color;
			}
			buffer += mb_info->framebuffer_pitch/4;
		}
	}
}

