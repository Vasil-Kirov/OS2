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
		return ERR_PTR(-EINVAL);

	void *kbuf = kmalloc(user_len);
	if (!kbuf)
		return ERR_PTR(-ENOMEM);
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
				return -EBADF;
			if (buf_len == 0)
				return 0;

			void *kbuf = kbuf_matching_len(buf_len);
			if (IS_ERR(kbuf))
				return PTR_ERR(kbuf);

			ssize_t read = vfs_read(proc->fds[fd], kbuf, buf_len);
			if (read < 0) {
				kfree(kbuf);
				return read;
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
				return -EBADF;

			if (buf_len == 0)
				return 0;

			void *kbuf = kmalloc(buf_len);
			if (!kbuf)
				return -ENOMEM;

			ssize_t copied = copy_from_user(kbuf, buf, buf_len);
			if (copied < 0) {
				kfree(kbuf);
				return copied;
			}

			ssize_t writen = vfs_write(proc->fds[fd], kbuf, buf_len);
			kfree(kbuf);
			return writen;
		} break;
		case SYS_open:
		{
			const void __user *name_ptr = (void __user *)ebx;
			const size_t name_len = ecx;
			const u16 flags = edx;
			(void)flags;

			if (name_len > KERNEL_MAX_NAME_LEN) {
				return -EINVAL;
			}

			ssize_t res = copy_from_user(buf, name_ptr, name_len);
			if (res < 0)
				return res;
			if (res < (ssize_t)name_len)
				return -EIO;

			int fd = -1;
			Process *proc = get_current_proc();
			for (int i = 0; i < MAX_FDS; ++i) {
				if (proc->fds[i] == NULL) {
					fd = i;
					break;
				}
			}
			if (fd == -1) {
				return -EMFILE;
			}

			const string_view name = {res, name_ptr};
			File *f = vfs_open(name);
			if (IS_ERR_OR_NULL(f))
				return PTR_ERR_OR(f, -ENOMEM);

			proc->fds[fd] = f;
			return fd;
		} break;
		case SYS_close:
		{
			int fd = ebx;
			Process *proc = get_current_proc();
			if (!verify_fd(proc, fd))
				return -EBADF;

			vfs_close(proc->fds[fd]);
			return 0;
		} break;
		case SYS_spawn:
		{
			const void __user *path_ptr = (void __user *)ebx;
			const size_t path_len = ecx;

			if (path_len > KERNEL_MAX_NAME_LEN) {
				return -EINVAL;
			}

			ssize_t res = copy_from_user(buf, path_ptr, path_len);
			if (res < 0)
				return res;
			if (res < (ssize_t)path_len)
				return -EIO;

			Process *proc = kzalloc(sizeof(Process));
			if (!proc)
				return -ENOMEM;

			const string_view path = {res, path_ptr};

			LoadProcessError err = load_proc(proc, path);
			switch (err)
			{
				case LoadProc_Ok:
				break;
				case LoadProc_OOM:
				kfree(proc);
				return -ENOMEM;
				default:
				kfree(proc);
				return -EINVAL;
			}

			scheduler_add_proc(proc);
			return proc->pid;
		} break;
		case SYS_getpid:
		{
			Process *proc = get_current_proc();
			if (!proc)
				return -EINVAL;
			return proc->pid;
		} break;
		case SYS_readdir:
		{
			int fd = ebx;
			void __user *buf = (void __user *)ecx;
			const size_t buf_len = edx;

			Process *proc = get_current_proc();
			if (!verify_fd(proc, fd))
				return -EBADF;
			if (buf_len == 0)
				return 0;
			if (!is_ssize_fit(buf_len))
				return -EINVAL;

			void *kbuf = kbuf_matching_len(buf_len*sizeof(DirInfo));
			if (IS_ERR(kbuf))
				return PTR_ERR(kbuf);
			ssize_t read = vfs_readdir(proc->fds[fd], kbuf, buf_len);
			if (read <= 0) {
				kfree(kbuf);
				return read;
			}
			DirInfo *arr = kbuf;
			for (ssize_t i = 0; i < read; ++i) {
				void *user_name = vmmap(&proc->vm, arr[i].name_len, PAGE_FLAG_RW);
				if (!user_name) {
					vfs_free_readdir_entries_vm(&proc->vm, arr, i);
					vfs_free_readdir_entries(arr + i, read-i);
					kfree(kbuf);
					return -ENOMEM;
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
					return copied;
				else
					return -EIO;
			}
			kfree(kbuf);
			return to_copy;
		} break;
	}
	return -ENOSYS;
}

