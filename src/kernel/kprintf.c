#include "kprintf.h"
#include <kcommon.h>
#include <stdarg.h>

KPrintConsole kprint_console;

static void print_ptr(uintptr_t val) {
	const char hex[] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};
	const size_t width = sizeof(uintptr_t) * 2;
	if (val == 0) {
		for (size_t i = 0; i < width; ++i) {
			kprint_console.write_char('0');
		}
		kprint_console.write_char('h');
		return;
	}

	char temp[sizeof(uintptr_t) * 2];
	size_t count = 0;
	while (val > 0) {
		unsigned digit = val & 0xF;
		val >>= 4;
		temp[count++] = hex[digit];
	}

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

void vkprintf(const char *fmt, va_list args)
{
	if(!kprint_console.write_char)
		return;

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
				case '[':
				{
					string_view v = {0, &fmt[i+1]};
					size_t end = i+1;
					for (; fmt[end] != '\0' && fmt[end] != ']'; ++end)
						;
					if (fmt[end] != ']') {
						kprint_console.write_char('%');
						kprint_console.write_char('[');
						continue;
					}
					v.count = end-(i+1);

					if(str_compare_const(&v, "str"))
					{
						const string_view err = STR_LIT("(null)");
						string_view s = va_arg(args, string_view);
						if (s.data == NULL)
							s = err;
						if (!s.data && s.count > 0)
							s = err;

						for(size_t i = 0; i < s.count; ++i)
							kprint_console.write_char(s.data[i]);
					}
					else
					{
						const char *s = "(unknown)";
						while (*s) {
							kprint_console.write_char(*s);
							s++;
						}
					}
					i = end;
				} break;
				case 's':
				{
					const char *s = va_arg(args, const char *);
					if (!s) {
						s = "(null)";
					}
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
}

void kprintf(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vkprintf(fmt, args);
	kprint_console.write_char('\n');
	va_end(args);
}

static char *curr_buf;
static size_t curr_size;
static size_t curr_at;
static void kprint_write_to_buf(char c)
{
	if (curr_at >= curr_size)
		return;
	curr_buf[curr_at++] = c;
}

void snprintf(char buf[], size_t size, const char *fmt, ...)
{
	// @TODO: hack
	curr_buf = buf;
	curr_size = size;
	curr_at = 0;
	KPrintConsole save = kprint_console;
	kprint_console.write_char = kprint_write_to_buf;

	va_list args;
	va_start(args, fmt);
	vkprintf(fmt, args);
	va_end(args);

	kprint_console = save;
}

