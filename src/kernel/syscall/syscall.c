#include "syscall.h"

#include <kmalloc.h>
#include <vfs.h>
#include <kmem.h>
#include <kcommon.h>
#include <kprintf.h>
#include <proc/proc.h>
#include <proc/loader.h>

static bool verify_fd(Process *proc, int fd)
{
	if (fd < 0 || fd >= MAX_FDS)
		return false;

	if (proc->fds[fd] == NULL)
		return false;
	return true;
}

static void *kbuf_matching_len(const size_t user_len)
{
#define MAX_USER_BUF (MB(64))
	if (user_len > MAX_USER_BUF)
		return ERR_PTR(-ErrSizeTooBig);

	void *kbuf = kmalloc(user_len);
	if (!kbuf)
		return ERR_PTR(-ErrOOM);
	return kbuf;
}

static bool is_ssize_fit(u32 r)
{
	return r <= SSIZE_MAX;
}

u32 handle_syscall(u32 eax, u32 ebx, u32 ecx, u32 edx, u32 esi, u32 edi)
{
	(void)esi;
	(void)edi;
	char buf[KERNEL_MAX_NAME_LEN];
	switch (eax)
	{
		case SYS_read:
		{
			int fd = ebx;
			void __user *buf = (void __user *)ecx;
			const size_t buf_len = edx;

			Process *proc = get_current_proc();
			if (!verify_fd(proc, fd))
				return -ErrBadFD;
			if (buf_len == 0)
				return 0;

			void *kbuf = kbuf_matching_len(buf_len);
			if (IS_ERR(kbuf))
				return PTR_ERR(kbuf);

			ssize_t read = vfs_read(proc->fds[fd], kbuf, buf_len);
			if (read < 0) {
				kfree(kbuf);
				return -ErrReadFailed;
			}
			ssize_t copied = copy_to_user(buf, kbuf, read);
			kfree(kbuf);
			return copied;
		} break;
		case SYS_write:
		{
			int fd = ebx;
			const void __user *buf = (const void __user *)ecx;
			const size_t buf_len = edx;

			Process *proc = get_current_proc();
			if (!verify_fd(proc, fd))
				return -ErrBadFD;

			if (buf_len == 0)
				return 0;

			void *kbuf = kmalloc(buf_len);
			if (!kbuf)
				return -ErrOOM;

			ssize_t copied = copy_from_user(kbuf, buf, buf_len);
			if (copied < 0) {
				kfree(kbuf);
				return -ErrInvalAddr;
			}

			ssize_t written = vfs_write(proc->fds[fd], kbuf, buf_len);
			kfree(kbuf);
			if (written < 0)
				return -ErrWriteFailed;
			return written;
		} break;
		case SYS_open:
		{
			const void __user *name_ptr = (void __user *)ebx;
			const size_t name_len = ecx;
			const u16 flags = edx;
			const u32 mode = esi;
			(void)flags;
			(void)mode;

			if (name_len > KERNEL_MAX_NAME_LEN) {
				return -ErrSizeTooBig;
			}

			ssize_t res = copy_from_user(buf, name_ptr, name_len);
			if (res < 0)
				return -ErrInvalAddr;
			if (res < (ssize_t)name_len)
				return -ErrReadFailed;

			int fd = -1;
			Process *proc = get_current_proc();
			for (int i = 0; i < MAX_FDS; ++i) {
				if (proc->fds[i] == NULL) {
					fd = i;
					break;
				}
			}
			if (fd == -1) {
				return -ErrTooManyFiles;
			}

			const string_view name = {res, name_ptr};
			File *f = vfs_open(name);
			if (IS_ERR_OR_NULL(f))
				return PTR_ERR_OR(f, -ErrOOM);

			proc->fds[fd] = f;
			return fd;
		} break;
		case SYS_close:
		{
			int fd = ebx;
			Process *proc = get_current_proc();
			if (!verify_fd(proc, fd))
				return -ErrBadFD;

			vfs_close(proc->fds[fd]);
			return 0;
		} break;
		case SYS_spawn:
		{
			const void __user *path_ptr = (void __user *)ebx;
			const size_t path_len = ecx;

			if (path_len > KERNEL_MAX_NAME_LEN) {
				return -ErrSizeTooBig;
			}

			ssize_t res = copy_from_user(buf, path_ptr, path_len);
			if (res < 0)
				return res;
			if (res < (ssize_t)path_len)
				return -ErrReadFailed;

			Process *proc = kzalloc(sizeof(Process));
			if (!proc)
				return -ErrOOM;

			const string_view path = {res, path_ptr};

			LoadProcessError err = load_proc(proc, path);
			if (err != LoadProc_Ok)
				kfree(proc);
			switch (err)
			{
				case LoadProc_Ok:
				break;

				case LoadProc_OOM:
				return -ErrOOM;

				case LoadProc_CorruptHeader:
				case LoadProc_FileCorrupt:
				return -ErrCorruptExec;

				case LoadProc_FileNotFound:
				return -ErrFileNotFound;

				case LoadProc_FileReadFailed:
				return -ErrReadFailed;

				case LoadProc_Incompatible:
				return -ErrIncompatibleExec;

				case LoadProc_NotExecutable:
				return -ErrNotExec;

				default:
				return -ErrNotExec;
			}
			Process *parent = get_current_proc();
			if (!parent)
				return -ErrUnexpectedSystemState;

			proc->parent = parent->pid;
			proc->fds[0] = vfs_dup(parent->fds[0]);
			proc->fds[1] = vfs_dup(parent->fds[1]);
			proc->fds[2] = vfs_dup(parent->fds[2]);

			scheduler_add_proc(proc);
			return proc->pid;
		} break;
		case SYS_getpid:
		{
			Process *proc = get_current_proc();
			if (!proc)
				return -ErrUnexpectedSystemState;
			return proc->pid;
		} break;
		case SYS_readdir:
		{
			int fd = ebx;
			void __user *buf = (void __user *)ecx;
			const size_t buf_len = edx;

			Process *proc = get_current_proc();
			if (!verify_fd(proc, fd))
				return -ErrBadFD;
			if (buf_len == 0)
				return 0;
			if (!is_ssize_fit(buf_len))
				return -ErrSizeTooBig;

			void *kbuf = kbuf_matching_len(buf_len*sizeof(DirInfo));
			if (IS_ERR(kbuf))
				return PTR_ERR(kbuf);
			ssize_t read = vfs_readdir(proc->fds[fd], kbuf, buf_len);
			if (read <= 0) {
				kfree(kbuf);
				return read;
			}
			if (buf == NULL) {
				kfree(kbuf);
				return read;
			}

			DirInfo *arr = kbuf;
			for (ssize_t i = 0; i < read; ++i) {
				void *user_name = vmmap(&proc->vm, arr[i].name_len, PAGE_FLAG_RW | PAGE_FLAG_US);
				if (!user_name) {
					vfs_free_readdir_entries_vm(&proc->vm, arr, i);
					vfs_free_readdir_entries(arr + i, read-i);
					kfree(kbuf);
					return -ErrOOM;
				}
				memcpy(user_name, arr[i].name, arr[i].name_len);
				kfree(arr[i].name);
				arr[i].name = user_name;
			}

			ssize_t to_copy = min(read, (ssize_t)buf_len);
			ssize_t copied = copy_to_user(buf, arr, to_copy * sizeof(DirInfo));
			if (copied != to_copy * (ssize_t)sizeof(DirInfo))
			{
				vfs_free_readdir_entries_vm(&proc->vm, arr, read);
				kfree(kbuf);
				if (copied < 0)
					return -ErrInvalAddr;
			}
			kfree(kbuf);
			return to_copy;
		} break;
		case SYS_mmap:
		{
			const size_t size = ebx;
			const u32 flags = ecx;
			int fd = edx;

			Process *proc = get_current_proc();
			if ((flags & MMAP_ANON) == 0 && !verify_fd(proc, fd))
				return 0;
			if (size == 0)
				return 0;

			if (flags & MMAP_ANON)
				fd = -1;

			return (u32)vm_reserve(&proc->vm, size, fd, flags);
		} break;
		case SYS_munmap:
		{
			const uintptr_t ptr = ebx;
			if (ptr == 0)
				return 0;

			Process *proc = get_current_proc();
			vmunmap(&proc->vm, (void *)ptr);

			return 0;
		} break;
		case SYS_exit:
		{
			const u32 code = ebx;
			(void)code;
			// @TODO:
			scheduler_remove_current();
			
		} break;
	}
	return -ENOSYS;
}

