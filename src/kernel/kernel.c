#include <kprintf.h>
#include <serial.h>
#include <kmem.h>
#include <kmalloc.h>
#include <multiboot.h>
#include <acpi.h>
#include <pci.h>
#include <gdt.h>
#include <interrupts.h>
#include <block.h>
#include <vfs.h>
#include <tmpfs.h>
#include <proc/loader.h>
#include <drivers.h>
#include <drivers/nvme.h>
#include <drivers/ext2.h>
#include <display/display.h>

__attribute__ ((noreturn)) 
void panic(const char *msg)
{
	(void)msg;
	for(;;)
		;
}

bool pcie_print_dev(PCIeDevDesc *dev, void *)
{
	const char *class_name = pcie_class_name(dev->class);
	size_t name_len = strlen(class_name);
	char pad[128] = {};
	for (size_t i = 0; i < ARRAY_COUNT(pad); ++i) {
		if (name_len+i > 32)
			break;
		pad[i] = ' ';
	}
	kprintf("\t%s%s| %s", class_name, pad, pcie_subclass_name(dev->class, dev->subclass));
	return false;
}

void kernel_main(uint32_t magic, multiboot_info *mb_info) 
{
	if(magic != MULTIBOOT_MAGIC)
		return;
	
	multiboot_framebuffer_tag *fb_tag = NULL;
	multiboot_mmap_tag *mmap_tag = NULL;
	multiboot_acpi_old_tag *acpio_tag = NULL;
	multiboot_acpi_new_tag *acpin_tag = NULL;
	multiboot_tag *tag = &mb_info->tags[0];
	while(tag->type != 0) {
		switch((MultibootTagTypes)tag->type) {
			case MBTag_MemoryMap:
				mmap_tag = (multiboot_mmap_tag *)tag;
				break;
			case MBTag_Framebuffer:
				fb_tag = (multiboot_framebuffer_tag *)tag;
				break;
			case MBTag_ACPIold:
				acpio_tag = (multiboot_acpi_old_tag *)tag;
				break;
			case MBTag_ACPInew:
				acpin_tag = (multiboot_acpi_new_tag *)tag;
				break;
			default:
			break;
		}
		tag = (multiboot_tag *)(ALIGN_UP((uintptr_t)tag + tag->size, 8));
	}
	(void)acpin_tag;

	if(!fb_tag || !mmap_tag || !acpio_tag)
		panic("Failed to find all tags!");

	if (init_serial() != 0)
		panic("Failed to init serial!");
	kprint_console.write_char = serial_write;

	kprintf("Initializing page table...");
	kmem_init(mmap_tag);

	kprintf("Initializing kernel heap...");
	kinit_heap();

	kprintf("Setting up GDT...");
	kgdt_setup();

	kprintf("Setting up interrupts...");
	kint_setup_interrupts(&acpio_tag->rsdp);

	if(!rsdp_check_header(&acpio_tag->rsdp))
		panic("Invalid RSDP header!");

	kprintf("Initializing PCIe...");
	PCIe *pci = pcie_init(&acpio_tag->rsdp);
	if(!pci)
		panic("Failed to init PCIe!");

	//kprintf("PCIe devices:");
	//pcie_iterate_entries(pci, pcie_print_dev, NULL);

	kprintf("Initializing VFS...");
	vfs_init();
	int r = tmpfs_init();
	if (r != 0) {
		kprintf("Failed to init tmpfs: %d", r);
		panic("TmpFS init failed!");
	}
	r = ext2_init();
	if (r != 0) {
		kprintf("Failed to init ext2: %d", r);
		panic("Ext2 init failed!");
	}
	r = vfs_mount_root(STR_LIT("tmpfs"));
	if (r != 0) {
		kprintf("Failed to mount root tmpfs: %d", r);
		panic("Root mount failed!");
	}
	r = vfs_setup_dev();
	if (r != 0) {
		kprintf("Failed to create /dev: %d", r);
		panic("File system init failed!");
	}
	r = drivers_init();
	if (r != 0) {
		kprintf("Failed to init drivers: %d", r);
		panic("Driver init failed!");
	}

	kprintf("Registering PCIe devices...");
	pcie_register_devices(pci);

	kprintf("Creating /mnt...");
	DirEntry *root = vfs_find(STR_LIT("/"));
	if (IS_ERR_OR_NULL(root)) {
		kprintf("Failed to find root in tmpfs: %d", PTR_ERR(root));
		panic("Finding root failed!");
	}
	string_view mnt_str = STR_LIT("mnt");
	DirEntry *mnt = root->inode->op.create(root, &mnt_str, 0777);
	if (IS_ERR_OR_NULL(mnt)) {
		kprintf("Failed to create /mnt in tmpfs: %d", PTR_ERR(mnt));
		panic("Creating /mnt failed!");
	}

	kprintf("Mounting disk...");
	r = vfs_mount(STR_LIT("ext2"), STR_LIT("/mnt"), STR_LIT("/dev/nvme0"), 0777);
	if (r != 0) {
		kprintf("Failed to mount ext2 fs: %d", r);
		panic("Ext2 mount failed!");
	}

	kprintf("Loading initial process...");
	Process *init_proc = kmem_map(sizeof(Process), PAGE_FLAG_RW);
	ASSERT(init_proc);
	LoadProcessError load_res = load_proc(init_proc, STR_LIT("/mnt/init"));
	if (load_res != LoadProc_Ok) {
		kprintf("Failed to load init proc: %d", load_res);
		panic("Failed to load init proc!");
	}

	kprintf("Initializing display...");
	r = display_init(fb_tag);
	if (r != 0) {
		kprintf("Failed to init display: %d", r);
	}
	kprintf("Enabling timer interrupts...");
	kint_start_timer();
	kprintf("Entering intial process...");
	enter_proc(init_proc);


#if 0
	kprintf("Kernel initialized!");
	u32 *color = kmem_map(sizeof(u32), PAGE_FLAG_RW);
	if(!color)
		panic("Failed to map kernel memory!");

	*color = 0x0000FFFF;

	// @Note: Maybe it should be MMIO, but not using cache on framebuffer writes
	// sounds very bad for performance, so maybe just find a way to flush it
	// Vasko - 22/07/2026
	uint32_t *framebuffer = kmem_map_phy_addr(fb_tag->framebuffer_addr, fb_tag->framebuffer_width * fb_tag->framebuffer_height * 4, PAGE_FLAG_RW);
	for(;;) {
		uint32_t *buffer = framebuffer;
		for(u32 y = 0; y < fb_tag->framebuffer_height/2; ++y) {
			for(u32 x = 0; x < fb_tag->framebuffer_width; ++x) {
				if (y % 2 == 0 || y % 3 == 0)
					buffer[x] = *color;
			}
			buffer += fb_tag->framebuffer_pitch/4;
		}
	}

	drivers_deinit();
#endif
}

