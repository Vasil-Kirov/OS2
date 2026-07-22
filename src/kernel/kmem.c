#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "kmem.h"


typedef struct {
	void *virtual_address;
	u32 *table;
} VirtualPageTable;

typedef struct KmemPhysicalFreeListNode {
	uintptr_t address;
	size_t size;
	size_t offset;
	struct KmemPhysicalFreeListNode *prev;
	struct KmemPhysicalFreeListNode *next;
} KmemPhysicalFreeListNode;

typedef struct KmemPhysicalFreeList {
	KmemPhysicalFreeListNode *head;
	KmemPhysicalFreeListNode *tail;
} KmemPhysicalFreeList;

typedef struct {
	KmemPhysicalFreeListNode *freelist;
	size_t size;
} KmemMapSignature;

typedef struct {
	uintptr_t physical_address;
	void *virtual_addr;
	KmemMapSignature sig;
	bool unmapped;
} DMAPageInfo;

KmemPhysicalFreeListNode initial_freelist_mem[128];

extern u32 paging_directory[1024];
VirtualPageTable kernel_virtual_pages[255];

u32 page_tables_data[255*1024] __attribute__((aligned(0x1000)));

void kmem_init_main_kernel_tables()
{
	for(size_t i = 0; i < ARRAY_COUNT(kernel_virtual_pages); ++i) {
		kernel_virtual_pages[i].table = &page_tables_data[i*1024];
		kernel_virtual_pages[i].virtual_address = kmem_add_page_table((u32)kernel_virtual_pages[i].table - KERNEL_OFFSET, 0x3);
	}
}

void *kmem_add_page_table(u32 table, uint16_t flags)
{
	static int directory_idx = 769;
	if(directory_idx == 1024)
		panic("Overflow kmem_add_page_table!");

	paging_directory[directory_idx++] = (u32)table | flags | PAGE_FLAG_PRESENT;
	return (void *)((directory_idx-1) * PAGE_SIZE * 1024);
}

void *kmem_map_phy_addr(uintptr_t physical_address, size_t size, uint16_t flags)
{
	uintptr_t page_start = physical_address & ~0xFFF;
	size_t offset = physical_address - page_start;
	size += offset;
	flags |= PAGE_FLAG_PRESENT;
	size_t needed_pages = (size + PAGE_SIZE-1) / PAGE_SIZE;
	void *res = NULL;
	u32 page_streak = 0;
	size_t i_start = ~0;
	size_t j_start = ~0;
	for(size_t i = 0; i < ARRAY_COUNT(kernel_virtual_pages) && !res; ++i) {
		for(size_t j = 0; j < 1024; ++j) {
			if(kernel_virtual_pages[i].table[j] != 0) {
				page_streak = 0;
				i_start = ~(size_t)0;
				j_start = ~(size_t)0;
			} else {
				if(i_start == ~(size_t)0) {
					i_start = i;
					j_start = j;
				}
				page_streak++;
				if(page_streak == needed_pages) {
					res = kernel_virtual_pages[i_start].virtual_address + j_start * PAGE_SIZE;
					for(size_t ati = i_start; ati <= i; ++ati) {
						for(size_t atj = j_start; atj < 1024; ++atj) {
							kernel_virtual_pages[ati].table[atj] = (page_start + ((ati-i_start)*PAGE_SIZE*1024) + ((atj-j_start) * PAGE_SIZE)) | flags;
							if(ati == i && atj == j)
								break;
						}
						j_start = 0;
					}
					break;
				}
			}
		}
	}

	if(!res)
		panic("Not enough reserved memory of kernel mappings!");

	return res + offset;
}

void kmem_unmap_raw(void *virtual_address, size_t size)
{
	if(virtual_address == NULL)
		return;

	void *page_start = (void *)((uintptr_t)virtual_address & ~0xFFF);
	size_t offset = virtual_address - page_start;
	size += offset;
	size_t needed_pages = (size + PAGE_SIZE-1) / PAGE_SIZE;
	size_t freed_pages = 0;
	for(size_t i = 0; i < ARRAY_COUNT(kernel_virtual_pages) && freed_pages < needed_pages; ++i) {
		if(kernel_virtual_pages[i].virtual_address + PAGE_SIZE * 1024 <= page_start)
			continue;

		if(page_start >= kernel_virtual_pages[i].virtual_address) {
			size_t idx = (page_start - kernel_virtual_pages[i].virtual_address) / PAGE_SIZE;
			for(size_t j = idx; j < 1024 && freed_pages < needed_pages; ++j) {
				kernel_virtual_pages[i].table[j] = 0;
				freed_pages++;
			}
		} else {
			for(size_t j = 0; j < 1024 && freed_pages < needed_pages; ++j) {
				kernel_virtual_pages[i].table[j] = 0;
				freed_pages++;
			}
		}
	}
}

KmemPhysicalFreeList physical_free_list;

void kmem_make_phy_page_table(multiboot_mmap_entry *mb_physical_mmap, u32 mb_physical_mmap_len)
{
	KmemPhysicalFreeListNode *at = physical_free_list.head;
	for(u32 i = 0; i < mb_physical_mmap_len; ++i) {
		// @NOTE: This shouldn't happen right? On a 32 bit system?

		// For a page to be usable it needs to be available, to not be a part of the kernel, and to be at least 4kb (1 page)
		if(mb_physical_mmap[i].type != MULTIBOOT_MEMORY_AVAILABLE)
			continue;

		uintptr_t addr = mb_physical_mmap[i].base_addr;
		size_t size = mb_physical_mmap[i].length;
		if(addr < PAGE_SIZE * 1024) {
			size_t to_offset = PAGE_SIZE * 1024 - addr;
			if(size <= to_offset)
				continue;
			size -= to_offset;
			addr += to_offset;
		}

		uintptr_t aligned_addr = ALIGN_UP(addr, PAGE_SIZE);
		size_t offset = aligned_addr - addr;

		if(size <= offset || size - offset < PAGE_SIZE)
			continue;

		at->address = aligned_addr;
		at->size = size - offset;
		at->offset = 0;
		at = at->next;
	}
}

#define __DMA_MAX_PAGES 8192
DMAPageInfo __dma_pages[__DMA_MAX_PAGES] = {};
size_t __dma_page_count = 0;

void dma_unmap(uintptr_t phy_addr)
{
	for(size_t i = 0; i < __dma_page_count; ++i)
	{
		if (__dma_pages[i].physical_address == phy_addr)
		{
			__dma_pages[i].unmapped = true;
			break;
		}
	}
}

bool dma_map(size_t size, void **virtual_addr, uintptr_t *phy_addr)
{
	if(size == 0) {
		if(virtual_addr)
			*virtual_addr = NULL;
		if(phy_addr)
			*phy_addr = 0;
		return true;
	}
	if (__dma_page_count >= 4096)
		return false;

	size = ALIGN_UP(size, PAGE_SIZE);
	for(size_t i = 0; i < __dma_page_count; ++i)
	{
		if (__dma_pages[i].unmapped && __dma_pages[i].sig.size == size)
		{
			__dma_pages[i].unmapped = false;
			if(virtual_addr)
				*virtual_addr = __dma_pages[i].virtual_addr;
			if(phy_addr)
				*phy_addr = __dma_pages[i].physical_address;
			return true;
		}
	}
	for(size_t i = 0; i < __dma_page_count; ++i)
	{
		if (__dma_pages[i].unmapped && __dma_pages[i].sig.size >= size)
		{
			__dma_pages[i].unmapped = false;
			if(virtual_addr)
				*virtual_addr = __dma_pages[i].virtual_addr;
			if(phy_addr)
				*phy_addr = __dma_pages[i].physical_address;
			return true;
		}
	}

	KmemPhysicalFreeListNode *at = physical_free_list.head;
	if(__dma_page_count >= __DMA_MAX_PAGES)
		return false;
	while(at) {
		if(at->address == 0)
			continue;
		if(at->offset >= at->size || at->offset - at->size < PAGE_SIZE)
			continue;
		if(size > at->size - at->offset)
			continue;
		uintptr_t address = at->address+at->offset;
		assert(address % PAGE_SIZE == 0);

		DMAPageInfo page = {};
		page.physical_address = address;
		page.virtual_addr = kmem_map_phy_addr(address, size, PAGE_FLAG_MMIO);
		at->offset += size;

		if(at->size - at->offset == 0) {
			if(at != physical_free_list.tail) {
				if(at->prev)
					at->prev->next = at->next;
				if(at->next)
					at->next->prev = at->prev;
				physical_free_list.tail->next = at;
				at->prev = physical_free_list.tail;
				physical_free_list.tail = at;
			}
		}
		page.sig.size = size;
		page.sig.freelist = at;
		page.unmapped = false;
		if(virtual_addr)
			*virtual_addr = page.virtual_addr;
		if(phy_addr)
			*phy_addr = page.physical_address;
		__dma_pages[__dma_page_count++] = page;
		memset(page.virtual_addr, 0, size);
		return true;
	}
	return false;
}

void *kmem_map(size_t size, u32 flags)
{
	if(size == 0)
		return NULL;

	size += sizeof(KmemMapSignature);
	KmemPhysicalFreeListNode *at = physical_free_list.head;
	while(at) {
		if(at->address == 0)
			continue;

		if(at->size - at->offset >= size) {
			uintptr_t address = at->address+at->offset;
			u32 page_start = address & ~0xFFF;
			u32 offset = address - page_start;
			size_t real_size = size + offset;
			real_size = ALIGN_UP(real_size, PAGE_SIZE);
			if(at->size - at->offset < real_size)
				continue;

			KmemMapSignature *signature = kmem_map_phy_addr(address, size, flags);
			at->offset += real_size;
			
			if(at->size - at->offset == 0) {
				if(at != physical_free_list.tail) {
					if(at->prev)
						at->prev->next = at->next;
					if(at->next)
						at->next->prev = at->prev;
					physical_free_list.tail->next = at;
					at->prev = physical_free_list.tail;
					physical_free_list.tail = at;
				}
			}
			signature->size = size - sizeof(KmemMapSignature);
			signature->freelist = at;
			return signature + 1;

		}
		at = at->next;
	}

	return NULL;
}

void kmem_unmap(void *ptr)
{
	// @TODO:
	if(!ptr)
		return;

	KmemMapSignature *sig = ptr;
	sig -= 1;

}

void kmem_init_freelists()
{
	physical_free_list.head = initial_freelist_mem;
	KmemPhysicalFreeListNode *prev = NULL;
	KmemPhysicalFreeListNode *at = physical_free_list.head;
	for(size_t i = 1; i < ARRAY_COUNT(initial_freelist_mem); ++i) {
		at->address = 0;
		at->next = initial_freelist_mem+i;
		at->prev = prev;
		at->size = 0;

		prev = at;
		at = at->next;
	}
	at->next = NULL;
	physical_free_list.tail = at;
}

void kmem_init(multiboot_mmap_tag *mmap)
{
	kmem_init_main_kernel_tables();
	kmem_init_freelists();
	kmem_make_phy_page_table(mmap->entries, (mmap->size - offsetof(multiboot_mmap_tag, entries)) / mmap->entry_size);
}

