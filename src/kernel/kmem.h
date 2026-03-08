
#ifndef _KMEM_H
#define _KMEM_H

#define KERNEL_OFFSET (0xC0000000)
#define PAGE_SIZE (0x1000)

#include "kcommon.h"
#include "multiboot.h"

void kmem_init(multiboot_info *mb);
void *kmem_add_page_table(u32 table, u16 flags);
void *kmem_map_phy_addr(uintptr_t physical_address, size_t size, u16 flags);
void *kmem_map(size_t size);
void kmem_unmap(void *ptr);



#endif // _KMEM_H

