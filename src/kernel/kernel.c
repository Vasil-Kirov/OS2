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

	for(;;)
	{
		uint32_t *buffer = (uint32_t*)mb_info->framebuffer_addr;
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

