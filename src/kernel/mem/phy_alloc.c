#include <sync/spinlock.h>
#include <kmem.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <kprintf.h>
#include <bstream.h>

typedef struct {
	PhyAddr base;
	size_t size;
} MemRegion;


#define MAX_INIT_REGIONS (128)
MemRegion initial_regions[MAX_INIT_REGIONS] = {};

MemRegion *mem_regions;
size_t mem_region_count;
ListNode mem_bitmap;
Spinlock pmm_lock;

typedef struct __attribute__((aligned(0x10))) {
	u32 *map;
	size_t page_count;
	size_t last_page;
	PhyAddr base;
	ListNode node;
} PMMBitmap;

bool pmm_find_init_region(PhyAddr *out_init, size_t *out_size)
{
	size_t prefer_size_list[] = {MB(32), MB(16), MB(8), MB(4), MB(2)};

	for (size_t il = 0; il < ARRAY_COUNT(prefer_size_list); ++il) {
		PhyAddr init_addr = {0};
		bool found_init = false;
		for (size_t i = 0; i < mem_region_count; ++i) {
			if (mem_regions[i].size >= prefer_size_list[il]) {
				init_addr = mem_regions[i].base;
				found_init = true;

				mem_regions[i].base.addr += prefer_size_list[il];
				mem_regions[i].size -= prefer_size_list[il];
				break;
			}
		}
		if (found_init) {
			*out_init = init_addr;
			*out_size = prefer_size_list[il];
			return true;
		}
	}
	return false;
}

#define PMM_REGION_RESERVED_BUMP_SIZE (KB(16))
bool pmm_map_region(BinaryStream *bump, MemRegion *region)
{
	if (region->size == 0)
		return false;

	PMMBitmap *bitmap = bs_rsz(bump, sizeof(PMMBitmap));
	if (!bitmap)
		return false;
	memset(bitmap, 0, sizeof(*bitmap));

	size_t map_size = region->size / (PAGE_SIZE * 8);

	bitmap->base = region->base;

	size_t remaining_bump_size = bs_remaining_size(bump);
	if (remaining_bump_size <= PMM_REGION_RESERVED_BUMP_SIZE)
		return false;

	while (map_size != 0 && remaining_bump_size - PMM_REGION_RESERVED_BUMP_SIZE <= map_size)
		map_size >>= 1;

	if (map_size == 0)
		return false;

	bitmap->map = bs_rsz(bump, map_size);
	if (!bitmap->map)
		return false;
	memset(bitmap->map, 0, map_size);

	bitmap->page_count = map_size * 8;
	list_init(&bitmap->node);
	list_add(&mem_bitmap, &bitmap->node);
	return true;
}

void pmm_init(size_t region_count)
{
	mem_regions = initial_regions;
	mem_region_count = region_count;
	list_init(&mem_bitmap);
	pmm_lock = SPINLOCK_INIT;

	PhyAddr init_addr = {0};
	size_t init_size = 0;
	bool found_init = pmm_find_init_region(&init_addr, &init_size);
	assert(found_init);

	// Cannot use kmem_map_phy_addr here because it requres the physical page allocator
	// to be online
	void *vaddr = vmmap_virtual_address_from_owned_physical(&kernel_address_space, init_addr.addr, init_size, PAGE_FLAG_RW);
	assert(vaddr);
	BinaryStream bump = {};
	bs_init(&bump, vaddr, init_size);

	size_t mapped_regions = 0;
	for (size_t i = 0; i < mem_region_count; ++i) {
		mapped_regions += pmm_map_region(&bump, &mem_regions[i]) ? 1 : 0;
	}
	assert(mapped_regions != 0);
}

void kmem_make_phy_page_table(multiboot_mmap_entry *mb_physical_mmap, u32 mb_physical_mmap_len)
{
	kprintf("Memory Map:");

	size_t regions_filled = 0;
	u32 i = 0;
	for(; i < mb_physical_mmap_len && regions_filled < MAX_INIT_REGIONS; ++i) {
		// For a page to be usable it needs to be available, to not be a part of the kernel, and to be at least 4kb (1 page)
		if(mb_physical_mmap[i].type != MULTIBOOT_MEMORY_AVAILABLE)
			continue;


		uintptr_t addr = mb_physical_mmap[i].base_addr;
		size_t size = mb_physical_mmap[i].length;
		kprintf("\tmemory: %p (%d)", addr, size);

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

		initial_regions[regions_filled++] = (MemRegion){
			.base = {aligned_addr},
			.size = size - offset,
		};
	}

	// @TODO: reallocate
	assert(i == mb_physical_mmap_len); // not overflowed physical pages
	pmm_init(regions_filled);
}

#if 0
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
	for(; at; at = at->next) {
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
#endif

static const size_t PAGES_PER_WORD = sizeof(*((PMMBitmap *)0)->map)*8;
static bool pmm_is_range_free(PMMBitmap *it, size_t start, size_t page_count)
{
	if (it->map[start] == ~(u32)0)
		return false;

	int zeros = 32;
	if (it->map[start] != 0)
		zeros = __builtin_clz(it->map[start]);
	if (zeros == 0)
		return false;

	size_t found_pages = zeros;
	for (size_t i = start+1; i < it->page_count && found_pages < page_count; ++i) {
		if (it->map[i] != 0) {
			found_pages += __builtin_ctz(it->map[i]);
			break;
		}
		found_pages += PAGES_PER_WORD;
	}

	return found_pages >= page_count;
}

static bool pmm_mark_range(PMMBitmap *it, size_t start, size_t page_count, int bit) 
{
	assert(bit == 0 || bit == 1);

	u32 first_word = bit == 1 ? it->map[start] : ~it->map[start];

	int start_mark;
	if (first_word == 0)
		start_mark = PAGES_PER_WORD;
	else
		start_mark = __builtin_clz(bit == 1 ? it->map[start] : ~it->map[start]);
	if (start_mark == 0)
		return false;


	size_t to_fill = page_count;
	for (int i = 0; i < start_mark && to_fill > 0; ++i) {
		if (bit == 1)
			it->map[start] |= 1u << (31 - i);
		else if(bit == 0)
			it->map[start] &= ~(1u << (31 - i));
		to_fill--;
	}

	for (size_t i = start+1; i < it->page_count >> 5 && to_fill > 0; ++i) {
		size_t biti = 0;
		while (to_fill > 0 && biti < sizeof(*it->map) * 8) {
			int bitpos = 1u << (31 - biti);
			if (bit == 1) {
				if ((it->map[i] & bitpos) != 0)
					return false;
			}
			else if (bit == 0) {
				if ((it->map[i] & bitpos) == 0)
					return false;
			}

			if (bit == 1)
				it->map[i] |= bitpos;
			else if (bit == 0)
				it->map[i] &= ~bitpos;

			biti++;
			to_fill--;
		}
	}

	return true;
}

PhyAddr pmm_alloc(size_t size, u32 flags)
{
	(void)flags;
	size_t page_count = INT_CEIL_DIV(size, PAGE_SIZE);
	if(page_count == 0)
		return (PhyAddr){0};

	PhyAddr res = {0};
	spin_lock(&pmm_lock);

	PMMBitmap *it;
	if (page_count < 24) {
		list_for_each_entry(it, &mem_bitmap, node) {
			bool found_non_full = false;
			for (size_t i = it->last_page >> 5; i < it->page_count >> 5; ++i) {
				u32 v = it->map[i];
				if (v == ~(u32)0) {
					continue;
				}
				v = ~v;
				for (size_t j = 0; j < page_count - 1; ++j) {
					v &= v >> 1;
				}
				if (v == 0) {
					found_non_full = true;
					continue;
				}

				size_t start = __builtin_ctz(v);
				size_t end = start + (page_count - 1);
				for (size_t j = start; j <= end; ++j)
					it->map[i] |= 1u << j;

				PhyAddr paddr = {it->base.addr + (i*PAGES_PER_WORD+start) * PAGE_SIZE};
				if (!found_non_full)
					it->last_page = i*PAGES_PER_WORD+start;

				res = paddr;
				goto unlock;
			}
		}
	} else {
		list_for_each_entry(it, &mem_bitmap, node) {
			bool found_non_full = false;
			for (size_t i = it->last_page >> 5; i < it->page_count >> 5; ++i) {
				if (pmm_is_range_free(it, i, page_count)) {
					int bit = 31;
					if (it->map[i] != 0)
					{
						bit = PAGES_PER_WORD - __builtin_clz(it->map[i]);
						assert(bit >= 0 && bit < (int)PAGES_PER_WORD);
					}
					PhyAddr paddr = {it->base.addr + (i*PAGES_PER_WORD+bit) * PAGE_SIZE};
					bool r = pmm_mark_range(it, i, page_count, 1);
					assert(r);
					if (!found_non_full)
						it->last_page = i*PAGES_PER_WORD+bit;

					res = paddr;
					goto unlock;
				}
				if (it->map[i] != ~(u32)0) {
					found_non_full = true;
				}
			}
		}

	}

unlock:
	spin_unlock(&pmm_lock);
	return res;
}

PhyAddr pmm_map_phy_addr(PhyAddr addr, size_t size)
{
	spin_lock(&pmm_lock);
	PhyAddr res = {0};

	PMMBitmap *it;
	list_for_each_entry(it, &mem_bitmap, node) {
		if (it->base.addr > addr.addr)
			continue;
		if (it->base.addr + it->page_count * PAGE_SIZE <= addr.addr)
			continue;

		// @TODO: Pages are lost here and cannot be restored
		// fix it
		// Vasko - 03/09/2026
		size_t page_idx = (addr.addr - it->base.addr) / PAGE_SIZE;
		size += (addr.addr - it->base.addr) % PAGE_SIZE;
		size_t page_word_idx = page_idx / 32;
		size_t page_bit_idx = page_idx % 32;
		while (size > 0 && page_bit_idx < 32) {
			if (it->map[page_word_idx] & (1 << page_bit_idx)) {
				goto unlock;
			}
			it->map[page_word_idx] |= 1 << page_bit_idx;
			page_bit_idx++;
			if (size < PAGE_SIZE)
				size = 0;
			else
				size -= PAGE_SIZE;
		}

		if (size > 0) {
			if (!pmm_mark_range(it, page_word_idx+1, size / PAGE_SIZE + 1, 1)) {
				goto unlock;
			}
		}
		res = addr;
		goto unlock;

	}

unlock:
	spin_unlock(&pmm_lock);
	return res;
}

void pmm_free(PhyAddr addr, size_t size)
{
	spin_lock(&pmm_lock);

	PMMBitmap *it;
	list_for_each_entry(it, &mem_bitmap, node) {
		if (it->base.addr > addr.addr)
			continue;
		if (it->base.addr + it->page_count * PAGE_SIZE <= addr.addr)
			continue;

		size_t page_idx = (addr.addr - it->base.addr) / PAGE_SIZE;
		size += (addr.addr - it->base.addr) % PAGE_SIZE;
		size_t page_word_idx = page_idx / 32;
		size_t page_bit_idx = page_idx % 32;
		while (size > 0 && page_bit_idx < 32) {
			it->map[page_word_idx] &= ~(1 << page_bit_idx);
			page_bit_idx++;
			if (size < PAGE_SIZE)
				size = 0;
			else
				size -= PAGE_SIZE;
		}
		if (size > 0) {
			pmm_mark_range(it, page_word_idx+1, size / PAGE_SIZE + 1, 0);
		}

		break;
	}

	spin_unlock(&pmm_lock);
}

void kmem_init_kernel_pages();
void kmem_init(multiboot_mmap_tag *mmap)
{
	kmem_init_kernel_pages();
	kmem_make_phy_page_table(mmap->entries, (mmap->size - offsetof(multiboot_mmap_tag, entries)) / mmap->entry_size);
}

