#include "kmem.h"
#include "kprintf.h"


typedef struct {
	u32 count;
	void *first;
} FreeStack;

typedef struct {
	u32 magic;
	i32 class;
} AllocHeader;

u8 *kheap_base=NULL;
u8 *kheap_at=NULL;
u8 *kheap_end=NULL;

#define ALLOC_MAGIC (0xDEADBEEF)

static const size_t HEAP_SIZE = MB(4);
static const size_t CLASS_SIZES[] = {16, 32, 64, 128, 256, 512, 1024};
FreeStack free_bins[ARRAY_COUNT(CLASS_SIZES)];

void kinit_heap()
{
	kheap_base = kmem_map(HEAP_SIZE, PAGE_FLAG_RW);
	if (!kheap_base)
		panic("Failed to initialize kernel heap!");
	kheap_at = kheap_base;
	kheap_end = kheap_base+HEAP_SIZE;
}

int class_for_size(size_t size)
{
	for (size_t i = 0; i < ARRAY_COUNT(CLASS_SIZES); ++i) {
		if (size <= CLASS_SIZES[i])
			return i;
	}
	return -1;
}


void *get_free_block(FreeStack *bin) {
	assert(bin->count > 0);

	bin->count--;
	
	void *block = bin->first;
	void *next = *(void **)block;
	if (bin->count != 0) {
		bin->first = next;
	} else {
		bin->first = 0;
	}
	return block;
}

void *kmalloc(size_t size)
{
	int class = class_for_size(size);
	void *mem = NULL;
	if (class == -1) {
		size_t alloc_size = ALIGN_UP(size+sizeof(AllocHeader), 16);
		mem = kmem_map(alloc_size, PAGE_FLAG_RW);
	} else {
		FreeStack *bin = &free_bins[class];
		if (bin->count != 0) {
			mem = get_free_block(bin);
		} else {
			size_t alloc_size = ALIGN_UP(CLASS_SIZES[class]+sizeof(AllocHeader), 16);
			mem = kheap_at;
			u8 *new_at = kheap_at + alloc_size;
			if (new_at > kheap_base+HEAP_SIZE || new_at < kheap_at) {
				kprintf("Heap buffer overflow in kmalloc.");
				return NULL;
			}
			kheap_at = new_at;
		}
	}

	if (mem) {
		AllocHeader *header = mem;
		header->magic = ALLOC_MAGIC;
		header->class = class;
		mem = header+1;
	}

	return mem;
}

void *kzalloc(size_t size)
{
	void *p = kmalloc(size);
	if (p)
		memset(p, 0, size);
	return p;
}

void kfree(void *p_)
{
	if (!p_)
		return;

	u8 *p = p_;
	bool in_heap = (p >= kheap_base + sizeof(AllocHeader) && p < kheap_end);
	if (!in_heap && (uintptr_t)p < KERNEL_OFFSET) {
		kprintf("kfree() called with invalid ptr %p.", p_);
		return;
	}

	AllocHeader *header = (AllocHeader *)(p-sizeof(AllocHeader));
	if (header->magic != ALLOC_MAGIC || (header->class != -1 && (size_t)header->class >= ARRAY_COUNT(CLASS_SIZES))) {
		kprintf("kfree() invalid header: %d %d.", header->magic, header->class);
		return;
	}

	if (header->class != -1 && !in_heap) {
		kprintf("kfree() called with invalid ptr %p for heap class %d.", p_, header->class);
		return;
	}

	header->magic = 0;
	if (header->class == -1) {
		kmem_unmap(header);
		return;
	}

	FreeStack *bin = &free_bins[header->class];
	bin->count++;
	*(void **)header = bin->first;
	bin->first = header;
}

