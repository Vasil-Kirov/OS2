#include <stddef.h>
#include <stdint.h>

extern uint32_t paging_directory[1024];
extern uint32_t kernel_page_table[1024];
static void *kernel_page_virtual_address;

void *kmem_add_page_table(uint32_t table)
{
	static int directory_idx = 767;
	paging_directory[directory_idx++] = (uint32_t)table;
	return (void *)((directory_idx-1) * 0x400000);
}

void *kmem_map(void *physical_address, size_t size)
{
	static int kernel_page_table_at = 0;

	uint32_t kernel_page_idx = kernel_page_table_at;
	uint32_t page_start = ((uint32_t)physical_address) & ~0xFFF;
	for(size_t i = 0; i < (size % 0x1000); ++i) {
		kernel_page_table[kernel_page_table_at++] = page_start + i * 0x1000;
	}

	return kernel_page_virtual_address + (kernel_page_idx * 0x1000);
}

