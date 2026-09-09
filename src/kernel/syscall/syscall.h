#include <kcommon.h>

typedef enum {
	SYS_read,   // (fd, buf, size) -> read
	SYS_write,  // (fd, buf, size) -> wirrten
	SYS_open,   // (name, name_len, flags) -> fd
	SYS_close,  // (fd)
	SYS_spawn,  // (path, path_len) -> pid
	SYS_getpid, // () -> pid
	SYS_readdir, // (fd, buf, count) -> read_count
} SYSCALLN;

#define KERNEL_MAX_NAME_LEN (256)

u32 handle_syscall(u32 eax, u32 ebx, u32 ecx, u32 edx, u32 esi, u32 edi);

