
#ifndef _KMEM_H
#define _KMEM_H

#include <sync/spinlock.h>
#include <kcommon.h>
#include <multiboot.h>
#include <list.h>

#define KERNEL_OFFSET ((uintptr_t)0xC0000000)
#define PAGE_SIZE (0x1000)

#if defined(__CHECKER__)
# define __kernel	__attribute__((address_space(0)))
# define __user		__attribute__((noderef, address_space(__user)))
# define __iomem	__attribute__((noderef, address_space(__iomem)))
#else
# define __kernel
# define __user
# define __iomem
#endif

void kmem_init(multiboot_mmap_tag *mmap);
void *kmem_map_phy_addr(uintptr_t physical_address, size_t size, u16 flags);
void kmem_unmap_raw(void *virtual_address, size_t size);
void *kmem_map(size_t size, u32 flags);
void kmem_unmap(void *ptr);

bool dma_map(size_t size, void **virtual_addr, uintptr_t *phy_addr);
void dma_unmap(void *vaddr);

#define PAGE_FLAG_PRESENT (1 << 0)
#define PAGE_FLAG_RW (1 << 1)
#define PAGE_FLAG_US (1 << 2) // (User=1/Supervisor=0)
#define PAGE_FLAG_WT (1 << 3)
#define PAGE_FLAG_CD (1 << 4)
#define PAGE_TABLE_ENTRY_MASK (0xFF)

#define MEM_FIRST_AVAIL_PAGE (4)

#define PAGE_FLAG_MMIO (PAGE_FLAG_WT | PAGE_FLAG_CD | PAGE_FLAG_RW)
#define PAGE_TABLES_COUNT (1024)

#define KB(n) ((n) << 10)
#define MB(n) ((n) << 20)
#define GB(n) ((n) << 30)

typedef struct { uintptr_t addr; } PhyAddr;

struct AddressSpace;

typedef enum {
	VMArea_Unused,
	VMArea_RAM,
} VMAreaType;

typedef struct VMRegion {
	struct AddressSpace *vma;
	VMAreaType type;
	PhyAddr paddr;
	void *vaddr;
	size_t size;
	struct VMRegion *left;
	struct VMRegion *right;
	struct VMRegion *parent;
	void *metadata_priv;
} VMRegion;

typedef struct VMPageTable {
	u32 *ptr;
} VMPageTable;

typedef struct AddressSpace {
	Spinlock lock;

	PhyAddr page_directory_paddr;
	u32 *page_directory;
	VMRegion *region_head;

	u16 dir_flags;

	VMPageTable tables[PAGE_TABLES_COUNT];
} AddressSpace;

typedef enum {
	PMM_None = 0x0,
	PMM_DMA  = 0x1, // @Note: right now doesn't do anything, but if I expand to 64 bits, it should only use memory below 4gb
} PMMFlags;

PhyAddr pmm_alloc(size_t size, u32 flags);
PhyAddr pmm_map_phy_addr(PhyAddr addr, size_t size);
void pmm_free(PhyAddr addr, size_t size);

typedef enum {
	MakeAddressSpace_Ok,
	MakeAddressSpace_OOM,
} MakeAddressSpaceError;

MakeAddressSpaceError make_address_space(AddressSpace *vm);
void free_address_space(AddressSpace *vm);

void *vmmap_(AddressSpace *vm, void *ataddr, size_t size, u32 flags, PhyAddr *out_addr);
void *vmmap(AddressSpace *vm, size_t size, u32 flags);
void vmunmap(AddressSpace *vm, void *ptr);

extern AddressSpace kernel_address_space;
void *vmmap_virtual_address_from_owned_physical(AddressSpace *ap, uintptr_t physical_address, size_t size, uint16_t flags);
void vmunmap_raw(AddressSpace *ap, void *virtual_address, size_t size, bool physical_free);

ssize_t copy_from_user(void *dst, const void __user *src, size_t len);
ssize_t copy_to_user(void __user *dst, const void *src, size_t len);

#endif // _KMEM_H

