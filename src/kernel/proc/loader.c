#include "loader.h"

#include <kmalloc.h>
#include <kcommon.h>
#include <vfs.h>
#include <bstream.h>
#include "kprintf.h"
#include "proc.h"
#include "elf.h"

LoadProcessError validate_header(Elf32_Ehdr *header)
{
	if (header->e_ident[EI_MAG0] != 0x7f || header->e_ident[EI_MAG1] != 'E' || header->e_ident[EI_MAG2] != 'L' || header->e_ident[EI_MAG3] != 'F') {
		return LoadProc_NotExecutable;
	}
	if (header->e_ident[EI_CLASS] != ELFCLASS32) {
		return LoadProc_Incompatible;
	}
	if (header->e_ident[EI_DATA] != ELFDATA2LSB) {
		return LoadProc_Incompatible;
	}
	if (header->e_phoff == 0) {
		return LoadProc_NotExecutable;
	}

	return LoadProc_Ok;
}

static void *map_at_and_copy(AddressSpace *vm, void *vaddr, size_t copy_size, size_t zero_size, void *buf, void **out_tmp, void **out_tmp_free)
{
	size_t total_size = copy_size+zero_size;
	PhyAddr paddr = {};
	void *ptr = vmmap_(vm, vaddr, total_size, PAGE_FLAG_US | PAGE_FLAG_RW | PAGE_FLAG_PRESENT, &paddr);
	if (!ptr)
		return NULL;

	size_t off = (uintptr_t)vaddr & (PAGE_SIZE-1);
	void *tmp = vmmap_virtual_address_from_owned_physical(&kernel_address_space, paddr.addr, total_size+off, PAGE_FLAG_RW | PAGE_FLAG_PRESENT);
	if (!tmp) {
		vmunmap(vm, ptr);
		return NULL;
	}
	void *tmp_free = tmp;
	tmp = (u8 *)tmp + off;
	memcpy(tmp, buf, copy_size);
	memset((u8 *)tmp+copy_size, 0, zero_size);
	if (out_tmp && out_tmp_free) {
		*out_tmp = tmp;
		*out_tmp_free = tmp_free;
	} else {
		vmunmap_raw(&kernel_address_space, tmp_free, total_size, false);
	}
	return ptr;
}

LoadProcessError load_proc(Process *proc, string_view path)
{
	LoadProcessError res = LoadProc_Ok;
	File *f = vfs_open(path);
	if (IS_ERR_OR_NULL(f))
		return LoadProc_FileNotFound;
	
	i64 fsize = f->inode->op.get_size(f->inode);
	if (fsize <= 0) {
		res = LoadProc_FileReadFailed;
		goto err_close_file;
	}

	void *buf = kzalloc(fsize);
	if (!buf) {
		res = LoadProc_OOM;
		goto err_close_file;
	}

	ssize_t read = vfs_read(f, buf, fsize);
	if (read < fsize) {
		res = LoadProc_FileReadFailed;
		goto err_free_buf;
	}

	BinaryStream bs = {};
	bs_init(&bs, buf, read);
	Elf32_Ehdr *header = bs_rsz(&bs, sizeof(Elf32_Ehdr));
	if (!header) {
		res = LoadProc_NotExecutable;
		goto err_free_buf;
	}
	LoadProcessError r = validate_header(header);
	if (r != 0) {
		res = r;
		goto err_free_buf;
	}

	if (make_address_space(&proc->vm) != MakeAddressSpace_Ok) {
		res = LoadProc_OOM;
		goto err_free_buf;
	}

	proc->stack = vmmap(&proc->vm, DEFAULT_STACK_SIZE, PAGE_FLAG_US | PAGE_FLAG_RW | PAGE_FLAG_PRESENT);
	if (!proc->stack) {
		res = -ENOMEM;
		goto err_free_vm;
	}
	proc->stack_top = (uintptr_t)proc->stack + DEFAULT_STACK_SIZE;
	proc->kernel_stack = kmem_map(PAGE_SIZE*4, PAGE_FLAG_US | PAGE_FLAG_RW | PAGE_FLAG_PRESENT);
	if (!proc->kernel_stack) {
		res = -ENOMEM;
		goto err_free_vm;
	}
	proc->kernel_stack_top = (uintptr_t)proc->kernel_stack + PAGE_SIZE*4;

	kprintf("Loading proc %[str]", path);
	kprintf("\tStack:   %p-%p", proc->stack, proc->stack_top);
	kprintf("\tKStack:  %p-%p", proc->kernel_stack, proc->kernel_stack_top);



	for (Elf32_Half i = 0; i < header->e_phnum; ++i) {
		if (!bs_seek(&bs, header->e_phoff + i*header->e_phentsize)) {
			res = LoadProc_FileCorrupt;
			goto err_free_vm;
		}

		Elf32_Phdr *ph = bs_rsz(&bs, sizeof(Elf32_Phdr));
		if (!ph) {
			res = LoadProc_FileCorrupt;
			goto err_free_vm;
		}

		switch(ph->p_type)
		{
			case PT_LOAD:
			{
				if (!bs_seek(&bs, ph->p_offset)) {
					res = LoadProc_FileCorrupt;
					goto err_free_vm;
				}
				void * seg = map_at_and_copy(&proc->vm, (void *)ph->p_vaddr, ph->p_filesz, ph->p_memsz-ph->p_filesz, bs.at, NULL, NULL);
				if (!seg) {
					res = LoadProc_OOM;
					goto err_free_vm;
				}
				kprintf("\tSegment: %p-%p", seg, (u8 *)seg + ph->p_memsz);
			} break;
			case PT_TLS:
			{
				proc->tls = (ThreadLocaLStorageInfo) {
					.template_ptr = (void __user *)ph->p_vaddr,
					.file_sz = ph->p_filesz,
					.mem_sz = ph->p_memsz,
					.align = ph->p_align,
				};
			} break;
		}
	}
	for (Elf32_Half i = 0; i < header->e_shnum; ++i) {
		if (!bs_seek(&bs, header->e_shoff + i*header->e_shentsize)) {
			res = LoadProc_FileCorrupt;
			goto err_free_vm;
		}

		Elf32_Shdr *sh = bs_rsz(&bs, sizeof(Elf32_Shdr));
		switch (sh->sh_type)
		{
			case SHT_SYMTAB:
			{
				size_t entry_count = sh->sh_size / sh->sh_entsize;
				if (!bs_seek(&bs, sh->sh_offset)) {
					res = LoadProc_FileCorrupt;
					goto err_free_vm;
				}
				Elf32_Sym *sym = bs_rsz(&bs, sizeof(Elf32_Sym));
				// @TODO:
				(void)sym;
				(void)entry_count;
			} break;
		}
	}

	for (size_t i = 0; i < MAX_FDS; ++i)
		proc->fds[i] = NULL;

	if (proc->tls.mem_sz > 0) {
		constexpr size_t TCB_SIZE = 64;

		size_t align = proc->tls.align ? proc->tls.align : 4;
		size_t memsz = ALIGN_UP(proc->tls.mem_sz, align);
		size_t zero = memsz - proc->tls.file_sz + TCB_SIZE;

		PhyAddr template_paddr = vm_get_phy_addr_from_virtual(&proc->vm, proc->tls.template_ptr);
		uintptr_t page_paddr = ALIGN_DOWN(template_paddr.addr, PAGE_SIZE);
		size_t off = template_paddr.addr - page_paddr;
		void *tmp = vmmap_virtual_address_from_owned_physical(&kernel_address_space, page_paddr, proc->tls.file_sz+off, PAGE_FLAG_RW | PAGE_FLAG_PRESENT);
		if (!tmp) {
			res = LoadProc_FileCorrupt;
			goto err_free_vm;
		}

		size_t to_free_size = memsz+TCB_SIZE;
		void *kernel_map = NULL;
		void *kernel_map_to_free = NULL;
		void *p = map_at_and_copy(&proc->vm, NULL, proc->tls.file_sz, zero, (u8 *)tmp+off, &kernel_map, &kernel_map_to_free);

		vmunmap_raw(&kernel_address_space, tmp, proc->tls.file_sz+off, false);

		if (!p) {
			vmunmap_raw(&kernel_address_space, kernel_map_to_free, to_free_size, false);
			res = LoadProc_FileCorrupt;
			goto err_free_vm;
		}

		proc->tp = (u8 *)p + memsz;
		void **self = (void **)((u8 *)kernel_map+memsz);
		*self = proc->tp;
		vmunmap_raw(&kernel_address_space, kernel_map_to_free, to_free_size, false);
	}

/*
    u32 es, ds;
    u32 edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;
    u32 vector, err_code;
    u32 eip, cs, eflags;
    u32 user_esp, user_ss;
 *
 */

	const u32 USER_CS = 3 << 3 | 3;
	const u32 USER_DS = 4 << 3 | 3;

	static int pid = 0;
	proc->pid = atomic_increment(&pid);
	proc->parent = -1;
	proc->entry = (void *)header->e_entry;

	InterruptFrame *frame = (InterruptFrame *)(proc->kernel_stack_top - sizeof(InterruptFrame));
	memset(frame, 0, sizeof(InterruptFrame));

	frame->eip = (uintptr_t)proc->entry;
	frame->cs = USER_CS;
	frame->eflags = 0x202; // int enabled, reserved = 1
	frame->user_esp = proc->stack_top;
	frame->user_ss = USER_DS;

	frame->ds = USER_DS;
	frame->es = USER_DS;
	proc->frame = frame;

	kprintf("\tEntry:   %p", proc->entry);
	kprintf("\tPID:     %d", proc->pid);
	kfree(buf);
	vfs_close(f);
	return LoadProc_Ok;

err_free_vm:
	free_address_space(&proc->vm);
err_free_buf:
	kfree(buf);
err_close_file:
	vfs_close(f);
	return res;
}



