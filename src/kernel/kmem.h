
#ifndef _KMEM_H
#define _KMEM_H

#define KERNEL_OFFSET (0xC0000000)

#include "kcommon.h"
#include "multiboot.h"

extern multiboot_memory_map_t *physical_mmap;
extern u32 physical_mmap_len;

void kmem_init_main_kernel_tables();
void *kmem_add_page_table(u32 table, u16 flags);
void *kmem_map_phy_addr(void *physical_address, size_t size, u16 flags);


#endif // _KMEM_H

