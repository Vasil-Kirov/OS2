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
	kprintf("\tStack:   %p", proc->stack_top);
	kprintf("\tKStack:  %p", proc->kernel_stack_top);



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
				PhyAddr paddr = {};
				void *seg = vmmap_(&proc->vm, (void *)ph->p_vaddr, ph->p_memsz, PAGE_FLAG_US | PAGE_FLAG_RW | PAGE_FLAG_PRESENT, &paddr);
				if (!seg) {
					res = LoadProc_OOM;
					goto err_free_vm;
				}
				if (!bs_seek(&bs, ph->p_offset)) {
					vmunmap(&proc->vm, seg);

					res = LoadProc_FileCorrupt;
					goto err_free_vm;
				}

				size_t off = ph->p_vaddr & (PAGE_SIZE-1);
				void *tmp = vmmap_virtual_address_from_owned_physical(&kernel_address_space, paddr.addr, ph->p_memsz+off, PAGE_FLAG_RW);
				if (!tmp) {
					vmunmap(&proc->vm, seg);

					res = LoadProc_OOM;
					goto err_free_vm;
				}
				tmp = (u8 *)tmp + off;
				memcpy(tmp, bs.at, ph->p_filesz);
				memset(tmp+ph->p_filesz, 0, ph->p_memsz-ph->p_filesz);
				vmunmap_raw(&kernel_address_space, tmp, ph->p_memsz, false);
				kprintf("\tSegment: %p", seg);
			} break;
		}
	}

	for (size_t i = 0; i < MAX_FDS; ++i)
		proc->fds[i] = NULL;

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



