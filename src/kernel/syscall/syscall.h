#include <kcommon.h>

typedef enum {
	SYS_read,   // (fd, buf, size) -> read
	SYS_write,  // (fd, buf, size) -> wirrten
	SYS_open,   // (name, name_len, flags, /* mode */...) -> fd
	SYS_close,  // (fd)
	SYS_spawn,  // (path, path_len) -> pid
	SYS_getpid, // () -> pid
	SYS_readdir, // (fd, buf, count) -> read_count
	SYS_mmap,    // (size, flags, fd) -> *void
	SYS_munmap,  // (ptr)
	SYS_exit,    // (code)
} SYSCALLN;

#define KERNEL_MAX_NAME_LEN (256)

u32 handle_syscall(u32 eax, u32 ebx, u32 ecx, u32 edx, u32 esi, u32 edi);


enum SysErr {
	ErrOk = 0,
	ErrBadFD = 1,
	ErrOOM = 2,
	ErrSizeTooBig = 3,
	ErrReadFailed = 4,
	ErrWriteFailed = 5,
	ErrInvalAddr = 6,
	ErrTooManyFiles = 7,

	ErrFileNotFound = 8,
	ErrCorruptExec = 9,
	ErrNotExec = 10,
	ErrIncompatibleExec = 11,
	
	ErrUnexpectedSystemState = 12,
};

enum OpenFlags {
	OpenReadOnly   = 0x00,
	OpenWriteOnly  = 0x01,
	OpenReadWrite  = 0x02,
	OpenCreate     = 0x04,
	OpenAlwaysOpen = 0x08,
	OpenTruncate   = 0x10,
	OpenAppend     = 0x20,
};

