#include <mem/page_fault.h>
#include <kmem.h>
#include <sync/spinlock.h>

#define PAGES_PER_METADATA_REGION (8)
#define METADATA_REGION_LEN ((PAGES_PER_METADATA_REGION * PAGE_SIZE) / sizeof(VMRegion))
typedef struct {
	PhyAddr paddr;
	size_t used;
	ListNode node;
	VMRegion regions[METADATA_REGION_LEN];
} MetadataAllocationRegion;

struct {
	Spinlock lock;
	ListNode head;
} vm_metadata;

extern u32 kernel_paging_directory[1024];
extern u32 boot_page_table[1024];

u32 kernel_page_tables_data[255*1024] __attribute__((aligned(0x1000)));

AddressSpace kernel_address_space = {};

static inline void native_flush_tlb_single(uintptr_t addr) {
   __asm__ volatile("invlpg [%0]" ::"r" (addr) : "memory");
}

static inline void flush_tlb() {
    unsigned long cr3;

    __asm__ volatile("mov %0, cr3" : "=r"(cr3));
    __asm__ volatile("mov cr3, %0" :: "r"(cr3) : "memory");
}

#define VADDR_FROM_DIR_IDX(IDX) ((void *)((IDX) * PAGE_SIZE * 1024))

static const size_t KERNEL_DIR_START_IDX = 769;
void add_kernel_to_address_space(AddressSpace *vm, u32 flags, bool add_boot_page)
{
	for(size_t i = KERNEL_DIR_START_IDX; i < PAGE_TABLES_COUNT; ++i) {
		u32 *table = &kernel_page_tables_data[(i-KERNEL_DIR_START_IDX) *1024];

		vm->page_directory[i] = ((u32)table - KERNEL_OFFSET) | flags;
		vm->tables[i].ptr = table;
	}

	if (add_boot_page) {
		u32 *table = &boot_page_table[0];
		vm->page_directory[KERNEL_DIR_START_IDX-1] = ((u32)table - KERNEL_OFFSET) | flags;
		vm->tables[KERNEL_DIR_START_IDX-1].ptr = table;
	}
}

void kmem_init_kernel_pages()
{
	list_init(&vm_metadata.head);
	vm_metadata.lock = SPINLOCK_INIT;

	kernel_address_space.lock = SPINLOCK_INIT;
	kernel_address_space.dir_flags = PAGE_FLAG_RW | PAGE_FLAG_PRESENT;

	kernel_address_space.page_directory = &kernel_paging_directory[0];
	for(size_t i = 0; i < PAGE_TABLES_COUNT; ++i) {
		kernel_address_space.tables->ptr = NULL;
	}
	add_kernel_to_address_space(&kernel_address_space, PAGE_FLAG_RW | PAGE_FLAG_PRESENT, false);
	flush_tlb();
}

bool vm_alloc_page_table(AddressSpace *vm, int at_idx, u16 flags)
{
	if (at_idx >= PAGE_TABLES_COUNT)
		return false;

	if (at_idx < 0) {
		for (size_t i = MEM_FIRST_AVAIL_PAGE; i < PAGE_TABLES_COUNT; ++i) {
			if (vm->tables[i].ptr == NULL) {
				at_idx = i;
				break;
			}
		}
	}

	if (at_idx < 0)
		return false;

	if (vm->tables[at_idx].ptr == NULL) {
		const size_t TABLE_SIZE = 1024 * sizeof(u32);
		PhyAddr paddr = {0};
		vm->tables[at_idx].ptr = vmmap_(&kernel_address_space, NULL, 1024 * sizeof(u32), PAGE_FLAG_RW, &paddr);
		ASSERT(vm->tables[at_idx].ptr);
		memset(vm->tables[at_idx].ptr, 0, TABLE_SIZE);
		vm->page_directory[at_idx] = paddr.addr | vm->dir_flags | flags | PAGE_FLAG_PRESENT;
	} else {
		vm->page_directory[at_idx] &= ~0xFFF;
		vm->page_directory[at_idx] |= vm->dir_flags | flags;
	}

	return true;
}

static bool can_address_space_use_table(AddressSpace *vm, u32 dir_entry)
{
	if (vm != &kernel_address_space && ((dir_entry & PAGE_FLAG_US) == 0))
		return false;
	return !((dir_entry & PAGE_FLAG_PRESENT) == 0 || ((dir_entry & PAGE_FLAG_RW) == 0));
}

void *vmmap_virtual_address_from_owned_physical(AddressSpace *vm, uintptr_t physical_address, size_t size, uint16_t flags)
{
	if (size == 0)
		return NULL;

	ASSERT(ALIGN_DOWN(physical_address, PAGE_SIZE) == physical_address);
	flags |= PAGE_FLAG_PRESENT;
	flags &= 0xFFF;
	size_t needed_pages = (size + PAGE_SIZE-1) / PAGE_SIZE;
	void *res = NULL;
	u32 page_streak = 0;
	size_t i_start = ~0;
	size_t j_start = ~0;
	for (size_t i = MEM_FIRST_AVAIL_PAGE; i < PAGE_TABLES_COUNT && !res; ++i) {
		if (vm->tables[i].ptr == NULL) {
			page_streak = 0;
			i_start = ~(size_t)0;
			j_start = ~(size_t)0;
			continue;
		}

		u32 dir_entry = vm->page_directory[i];
		if (!can_address_space_use_table(vm, dir_entry)) {
			page_streak = 0;
			i_start = ~(size_t)0;
			j_start = ~(size_t)0;
			continue;
		}

		for(size_t j = 0; j < 1024; ++j) {
			if(vm->tables[i].ptr[j] != 0) {
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
					res = VADDR_FROM_DIR_IDX(i_start) + j_start * PAGE_SIZE;
					size_t k = 0;
					for(size_t ati = i_start; ati <= i; ++ati) {
						for(size_t atj = j_start; atj < 1024; ++atj) {
							vm->tables[ati].ptr[atj] = (physical_address + k * PAGE_SIZE) | flags;
							native_flush_tlb_single((uintptr_t)res + k * PAGE_SIZE);
							++k;
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

	if(!res) {
		if (vm == &kernel_address_space)
			panic("Kernel address space ran out of memory!");

		if (vm_alloc_page_table(vm, -1, PAGE_FLAG_RW)) {
			return vmmap_virtual_address_from_owned_physical(vm, physical_address, size, flags);
		}
		return NULL;
	}

	return res;
}

void *vmmap_at_virtual_address(AddressSpace *vm, void *vaddr, PhyAddr address, size_t size, uint16_t flags)
{
	if (size == 0)
		return NULL;

	void *page_start = (void *)ALIGN_DOWN((uintptr_t)vaddr, PAGE_SIZE);

	size_t written_pages = 0;
	for (size_t i = 0; i < PAGE_TABLES_COUNT; ++i) {
		void *base = VADDR_FROM_DIR_IDX(i);
		if (page_start >= base + 1024 * PAGE_SIZE || base >= page_start+size)
			continue;

		if (vm->tables[i].ptr == NULL) {
			vm_alloc_page_table(vm, i, PAGE_FLAG_RW);
			if (vm->tables[i].ptr == NULL)
				return NULL;
		}

		int page_idx = (page_start - base) / PAGE_SIZE;
		int page_end = INT_CEIL_DIV((page_start+size) - base, PAGE_SIZE);
		if (page_idx < 0) page_idx = 0;
		if (page_end > 1024) page_end = 1024;

		for (; page_idx < page_end; ++page_idx) {
			// @Note: Should this fail on present pages?
			// Vasko - 05/09/2026
			vm->tables[i].ptr[page_idx] = (address.addr+written_pages*PAGE_SIZE) | flags;
			written_pages++;
		}
	}
	size_t needed_pages = INT_CEIL_DIV(size, PAGE_SIZE);
	if (needed_pages > written_pages)
		return NULL;

	ASSERT(written_pages == needed_pages);
	return vaddr;
}

void *vmmap_phy_addr(AddressSpace *ap, uintptr_t physical_address, size_t size, uint16_t flags)
{
	uintptr_t page_start = ALIGN_DOWN(physical_address, PAGE_SIZE);
	PhyAddr paddr = {.addr=page_start};
	size = ALIGN_UP(size, PAGE_SIZE);
	
	// @Note: return value is not checked, since the requested address may be outside of
	// the memory region marked as available by the memory map.
	// In that case this map and the later free will just fail and the kernel
	// will continue running like nothing happened. This is a completely normal operation.
	pmm_map_phy_addr(paddr, size);

	u8 *p = vmmap_virtual_address_from_owned_physical(ap, page_start, size, flags);
	if (!p)
		return NULL;
	return p + (physical_address - page_start);
}

void vmunmap_raw(AddressSpace *ap, void *virtual_address, size_t size, bool physical_free)
{
	if(virtual_address == NULL)
		return;

	void *page_start = (void *)((uintptr_t)virtual_address & ~0xFFF);
	size_t offset = virtual_address - page_start;
	size += offset;
	size_t needed_pages = (size + PAGE_SIZE-1) / PAGE_SIZE;
	size_t freed_pages = 0;
	for(size_t i = 0; i < PAGE_TABLES_COUNT && freed_pages < needed_pages; ++i) {
		if (ap->tables[i].ptr == NULL)
			continue;

		void *vaddr = VADDR_FROM_DIR_IDX(i);
		if(vaddr + PAGE_SIZE * 1024 <= page_start)
			continue;

		if(page_start >= vaddr) {
			size_t idx = (page_start - vaddr) / PAGE_SIZE;
			for(size_t j = idx; j < 1024 && freed_pages < needed_pages; ++j) {
				ap->tables[i].ptr[j] = 0;
				if (freed_pages == 0 && physical_free) {
					PhyAddr addr = {ap->tables[i].ptr[idx] & ~0xFFF};
					pmm_free(addr, size);
				}
				freed_pages++;
			}
		} else {
			for(size_t j = 0; j < 1024 && freed_pages < needed_pages; ++j) {
				ap->tables[i].ptr[j] = 0;
				if (freed_pages == 0 && physical_free) {
					PhyAddr addr = {ap->tables[i].ptr[0] & ~0xFFF};
					pmm_free(addr, size);
				}
				freed_pages++;
			}
		}
	}
}

VMRegion *vm_find_or_add_metdata_region()
{
	spin_lock(&vm_metadata.lock);
	VMRegion *res = NULL;

	MetadataAllocationRegion *it;
	list_for_each_entry(it, &vm_metadata.head, node) {
		if (it->used >= METADATA_REGION_LEN)
			continue;

		for (size_t i = 0; i < ARRAY_COUNT(it->regions); ++i) {
			if (it->regions[i].type == VMArea_Unused) {
				it->regions[i].type = VMArea_RAM;
				res = &it->regions[i];
				res->metadata_priv = it;
				it->used++;
				goto unlock;
			}
		}
	}


	ASSERT(res == NULL);

	PhyAddr addr = pmm_alloc(sizeof(MetadataAllocationRegion), PMM_None);
	if (addr.addr == 0)
		goto unlock;

	MetadataAllocationRegion *region = vmmap_virtual_address_from_owned_physical(&kernel_address_space, addr.addr, sizeof(MetadataAllocationRegion), PAGE_FLAG_RW);
	if (!region)
		goto unlock;

	list_init(&region->node);
	list_add(&vm_metadata.head, &region->node);
	res = &region->regions[0];
	res->metadata_priv = region;
	region->used++;


unlock:
	spin_unlock(&vm_metadata.lock);
	return res;
}

void vm_add_region(AddressSpace *vm, VMRegion *region)
{
	spin_lock(&vm->lock);
	region->left = NULL;
	region->right = NULL;

	if (vm->region_head == NULL) {
		region->parent = NULL;
		vm->region_head = region;
		goto unlock;
	}

	VMRegion *at = vm->region_head;
	while (true) {
		if (region->vaddr > at->vaddr) {
			if (at->right) {
				at = at->right;
				continue;
			}
			region->parent = at;
			at->right = region;
			break;
		}
		else {
			if (at->left) {
				at = at->left;
				continue;
			}
			region->parent = at;
			at->left = region;
			break;
		}
	}

unlock:
	spin_unlock(&vm->lock);
}

void *vmmap_(AddressSpace *vm, void *ataddr, size_t size, u32 flags, PhyAddr *out_addr)
{
	size += (uintptr_t)ataddr - ALIGN_DOWN((uintptr_t)ataddr, PAGE_SIZE);
	size = ALIGN_UP(size, PAGE_SIZE);
	PhyAddr addr = pmm_alloc(size, PMM_None);
	if (addr.addr == 0)
		return NULL;
	ASSERT(addr.addr % PAGE_SIZE == 0);

	VMRegion *region = vm_find_or_add_metdata_region();
	if (!region) {
		pmm_free(addr, size);
		return NULL;
	}

	void *vaddr = NULL;

	if (ataddr == NULL) {
		vaddr = vmmap_virtual_address_from_owned_physical(vm, addr.addr, size, flags);
	} else {
		vaddr = vmmap_at_virtual_address(vm, ataddr, addr, size, flags);
	}
	if (!vaddr) {
		pmm_free(addr, size);
		region->type = VMArea_Unused;
		return NULL;
	}

	region->vma = vm;
	region->type = VMArea_RAM;
	region->paddr = addr;
	region->vaddr = vaddr;
	region->size = size;

	vm_add_region(vm, region);

	if (out_addr)
		*out_addr = addr;
	return vaddr;
}

void *vmmap(AddressSpace *vm, size_t size, u32 flags)
{
	return vmmap_(vm, NULL, size, flags, NULL);
}

void vm_replace_node(AddressSpace *vm, VMRegion *node, VMRegion *with)
{
	if (node->parent == NULL) {
		vm->region_head = with;
	} else if (node->parent->left == node) {
		node->parent->left = with;
	} else {
		ASSERT(node->parent->right == node);
		node->parent->right = with;
	}

	if (with != NULL)
		with->parent = node->parent;
}

void vm_remove_region(AddressSpace *vm, VMRegion *region)
{
	if (region->left == NULL) {
		vm_replace_node(vm, region, region->right);
	} else if (region->right == NULL) {
		vm_replace_node(vm, region, region->left);
	} else {
		VMRegion *at = region->left;
		while (at->right) {
			at = at->right;
		}

		if (at->parent->right == at)
			at->parent->right = at->left;
		else
			at->parent->left = at->left;

		if (at->left)
			at->left->parent = at->parent;
		vm_replace_node(vm, region, at);
		if (region->left != at)
			at->left = region->left;
		at->right = region->right;

		if (at->left)
			at->left->parent = at;
		if (at->right)
			at->right->parent = at;
	}
	*region = (VMRegion){};
	region->type = VMArea_Unused;
}

void vmunmap(AddressSpace *vm, void *ptr)
{
	spin_lock(&vm->lock);
	VMRegion *at = vm->region_head;
	while (at) {
		if (ptr > at->vaddr) {
			at = at->right;
		} else if (ptr < at->vaddr) {
			at = at->left;
		} else {
			ASSERT(ptr == at->vaddr);
			vmunmap_raw(vm, ptr, at->size, true);
			vm_remove_region(vm, at);
			break;
		}
	}
	spin_unlock(&vm->lock);
}

void *kmem_map_phy_addr(uintptr_t physical_address, size_t size, u16 flags)
{
	return vmmap_phy_addr(&kernel_address_space, physical_address, size, flags);
}

void kmem_unmap_raw(void *virtual_address, size_t size)
{
	return vmunmap_raw(&kernel_address_space, virtual_address, size, true);
}

void *kmem_map(size_t size, u32 flags)
{
	return vmmap(&kernel_address_space, size, flags);
}

void kmem_unmap(void *ptr)
{
	return vmunmap(&kernel_address_space, ptr);
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

	PhyAddr paddr;
	void *vaddr = vmmap_(&kernel_address_space, NULL, size, PAGE_FLAG_MMIO, &paddr);
	if (!vaddr)
		return false;

	if(virtual_addr)
		*virtual_addr = vaddr;
	if(phy_addr)
		*phy_addr = paddr.addr;
	return true;
}

void dma_unmap(void *vaddr)
{
	vmunmap(&kernel_address_space, vaddr);
}

MakeAddressSpaceError make_address_space(AddressSpace *vm)
{
	memset(vm, 0, sizeof(AddressSpace));
	MakeAddressSpaceError res = MakeAddressSpace_Ok;
	vm->page_directory = vmmap_(&kernel_address_space, NULL, sizeof(u32) * 1024, PAGE_FLAG_RW, &vm->page_directory_paddr);
	if (!vm->page_directory) {
		res = MakeAddressSpace_OOM;
		goto err_exit;
	}
	memset(vm->page_directory, 0, sizeof(u32) * 1024);

	vm->dir_flags = PAGE_FLAG_US | PAGE_FLAG_RW | PAGE_FLAG_PRESENT;

	add_kernel_to_address_space(vm, PAGE_FLAG_RW | PAGE_FLAG_PRESENT, true);

	vm->region_head = NULL;
	vm->lock = SPINLOCK_INIT;


	return MakeAddressSpace_Ok;

err_exit:
	return res;
}

void free_address_space(AddressSpace *vm)
{
	// @TODO: unmap all entries in tables
	// @LEAK
	for (size_t i = 0; i < KERNEL_DIR_START_IDX; ++i) {
		if (vm->tables[i].ptr)
			kmem_unmap(vm->tables[i].ptr);
	}
	kmem_unmap(vm->tables);
	kmem_unmap(vm->page_directory);
}

static bool access_ok(const void *ptr, size_t len)
{
    uintptr_t addr = (uintptr_t)ptr;
    if (len > KERNEL_OFFSET)
        return false;
    if (addr >= KERNEL_OFFSET)
        return false;
    if (addr + len > KERNEL_OFFSET)
        return false;
    return true;
}

ssize_t copy_from_user(void *dst, const void __user *src, size_t len)
{
	if (!access_ok(src, len))
		return -EFAULT;

	u8 *dstp = dst;
	const u8 __user *srcp = src;
	size_t n = 0;
	for (; n < len; ++n) {
		*dstp = __user_read(srcp, fault);
		dstp++;
		srcp++;
	}

	return n;

fault:
	return -EFAULT;
}

ssize_t copy_to_user(void __user *dst, const void *src, size_t len)
{
	if (!access_ok(dst, len))
		return -EFAULT;

	u8 __user *dstp = dst;
	const u8 *srcp = src;
	size_t n = 0;
	for (; n < len; ++n) {
		__user_write(dstp, *srcp, fault);
		dstp++;
		srcp++;
	}

	return n;

fault:
	return -EFAULT;
}

