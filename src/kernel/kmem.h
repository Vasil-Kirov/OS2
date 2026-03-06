
#ifndef _KMEM_H
#define _KMEM_H

#include <stdint.h>
#include <stddef.h>

static void *kernel_page_virtual_address;
extern uint32_t kernel_page_table[1024];

void *kmem_add_page_table(uint32_t table);
void *kmem_map(void *physical_address, size_t size);

#endif // _KMEM_H

