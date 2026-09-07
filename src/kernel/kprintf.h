
#ifndef _KPRINTF_H
#define _KPRINTF_H

#include <kcommon.h>

typedef struct {
	void (*write_char)(char c);
} KPrintConsole;

extern KPrintConsole kprint_console;

void kprintf(const char *fmt, ...);
void snprintf(char buf[], size_t size, const char *fmt, ...);


#endif

