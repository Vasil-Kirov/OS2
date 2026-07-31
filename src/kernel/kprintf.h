
#ifndef _KPRINTF_H
#define _KPRINTF_H


typedef struct {
	void (*write_char)(char c);
} KPrintConsole;

extern KPrintConsole kprint_console;

void kprintf(const char *fmt, ...);


#endif

