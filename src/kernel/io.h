
#ifndef _IO_H
#define _IO_H

#include "kcommon.h"


u32 in32(u16 port) __attribute__((fastcall));
void out32(u16 port, u32 dword) __attribute__((fastcall));


#endif


