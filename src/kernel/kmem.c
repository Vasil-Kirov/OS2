#include <stddef.h>
#include <stdint.h>
#include "kmem.h"


typedef struct VirtualPageTable {
	void *virtual_address;
	u32 *table;
	int at;
} VirtualPageTable;

extern u32 paging_directory[1024];
multiboot_memory_map_t *physical_mmap;
u32 physical_mmap_len;

VirtualPageTable kernel_virtual_pages[255];

u32 page_tables_data[255*1024] __attribute__((aligned(0x1000)));

void kmem_init_main_kernel_tables()
{
	for(size_t i = 0; i < ARRAY_COUNT(kernel_virtual_pages); ++i) {
		kernel_virtual_pages[i].table = &page_tables_data[i*1024];
		kernel_virtual_pages[i].at = 0;
		kernel_virtual_pages[i].virtual_address = kmem_add_page_table((u32)kernel_virtual_pages[i].table - KERNEL_OFFSET, 0x3);
	}
}

void *kmem_add_page_table(u32 table, uint16_t flags)
{
	static int directory_idx = 769;
	if(directory_idx == 1024)
		panic("Overflow kmem_add_page_table!");

	paging_directory[directory_idx++] = (u32)table | flags;
	return (void *)((directory_idx-1) * 0x400000);
}

void *kmem_map_phy_addr(void *physical_address, size_t size, uint16_t flags)
{
	u32 address_int = (u32)physical_address;
	u32 page_start = address_int & ~0xFFF;
	u32 offset = address_int - page_start;
	size += offset;
	size_t written = 0;
	void *res = NULL;
	for(size_t j = 0; j < ARRAY_COUNT(kernel_virtual_pages) && written < size; ++j) {
		while(kernel_virtual_pages[j].at < 1024 && written < size) {
			if(!res)
				res = kernel_virtual_pages[j].virtual_address + kernel_virtual_pages[j].at * 0x1000;

			kernel_virtual_pages[j].table[kernel_virtual_pages[j].at++] = (page_start + written) | flags;
			written += 0x1000;
		}
	}

	if(written < size)
		panic("Not enough reserved memory of kernel mappings!");

	return res + offset;
}

void kmem_map(size_t size)
{
	for(u32 i = 0; i < physical_mmap_len; ++i) {
		//physical_mmap[i].
	}
}

