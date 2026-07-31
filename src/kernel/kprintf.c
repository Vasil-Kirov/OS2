#include "kprintf.h"
#include <kcommon.h>
#include <stdarg.h>

KPrintConsole kprint_console;

static void print_ptr(uintptr_t val) {
	const char hex[] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};
	if (val == 0) {
		kprint_console.write_char('0');
		kprint_console.write_char('h');
		return;
	}

	char temp[128];
	size_t count = 0;
	while (val > 0) {
		unsigned digit = val & 0xF;
		val >>= 4;
		temp[count++] = hex[digit];
	}

	size_t width = sizeof(uintptr_t) * 2;
	for (size_t i = count; i < width; ++i) {
		kprint_console.write_char('0');
	}
	for (size_t i = 0; i < count; ++i) {
		kprint_console.write_char(temp[count - i - 1]);
	}
	kprint_console.write_char('h');
}

static void print_int(int val) {
	if (val == 0) {
		kprint_console.write_char('0');
		return;
	}

    unsigned int u;
    if (val < 0) {
        kprint_console.write_char('-');
        u = (unsigned int)(-(val + 1)) + 1;
    } else {
        u = (unsigned int)val;
    }

	char temp[128];
	size_t count = 0;
	while (u > 0) {
		int digit = u % 10;
		u /= 10;
		char c = '0' + digit;
		temp[count++] = c;
	}

	for (size_t i = 0; i < count; ++i) {
		kprint_console.write_char(temp[count - i - 1]);
	}
}

void kprintf(const char *fmt, ...)
{
	if(!kprint_console.write_char)
		return;

	va_list args;
	va_start(args, fmt);

	for (size_t i = 0; fmt[i] != 0; ++i) {
		if (fmt[i] == '%') {
			++i;
			switch(fmt[i]) {
				case 'd':
				{
					int val = va_arg(args, int);
					print_int(val);
				} break;
				case 'p':
				{
					uintptr_t val = va_arg(args, uintptr_t);
					print_ptr(val);
				} break;
				case 's':
				{
					const char *s = va_arg(args, const char *);
					while (*s) {
						kprint_console.write_char(*s);
						s++;
					}

				} break;
				case '%':
				{
					kprint_console.write_char('%');
				} break;
				case 0:
				{
					kprint_console.write_char('%');
					i--;
				} break;
				default:
				{
					kprint_console.write_char('%');
					kprint_console.write_char(fmt[i]);
				} break;
			}
		} else {
			kprint_console.write_char(fmt[i]);
		}
	}
	kprint_console.write_char('\n');

	va_end(args);
}

