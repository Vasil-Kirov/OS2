
#ifndef _LOADER_H
#define _LOADER_H

#include "proc.h"
#include <kcommon.h>

typedef enum {
	LoadProc_Ok,
	LoadProc_FileNotFound,
	LoadProc_OOM,
	LoadProc_FileReadFailed,
	LoadProc_CorruptHeader,
	LoadProc_NotExecutable,
	LoadProc_Incompatible,
	LoadProc_FileCorrupt,
} LoadProcessError;

LoadProcessError load_proc(Process *proc, string_view path);

#endif

