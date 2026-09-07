#include <kcommon.h>

typedef enum {
	SYS_read,   // (fd, buf, size)
	SYS_write,  // (fd, buf, size)
	SYS_open,   // (name, name_len, flags)
	SYS_close,  // (fd)
	SYS_spawn,  // (path, path_len)
	SYS_getpid,
} SYSCALLN;

#define KERNEL_MAX_NAME_LEN (256)

u32 handle_syscall(u32 eax, u32 ebx, u32 ecx, u32 edx, u32 esi, u32 edi);

