#include "serial.h"
#include <kmalloc.h>
#include <errno.h>
#include <kcommon.h>
#include <io.h>
#include <drivers.h>

#define COM1 0x3F8

int init_serial() {
   out8(COM1 + 1, 0x00);    // Disable all interrupts
   out8(COM1 + 3, 0x80);    // Enable DLAB (set baud rate divisor)
   out8(COM1 + 0, 0x03);    // Set divisor to 3 (lo byte) 38400 baud
   out8(COM1 + 1, 0x00);    //                  (hi byte)
   out8(COM1 + 3, 0x03);    // 8 bits, no parity, one stop bit
   out8(COM1 + 2, 0xC7);    // Enable FIFO, clear them, with 14-byte threshold
   out8(COM1 + 4, 0x0B);    // IRQs enabled, RTS/DSR set
   out8(COM1 + 4, 0x1E);    // Set in loopback mode, test the serial chip
   out8(COM1 + 0, 0xAE);    // Test serial chip (send byte 0xAE and check if serial returns same byte)

   // Check if serial is faulty (i.e: not same byte as sent)
   if(in8(COM1 + 0) != 0xAE) {
      return -ENODEV;
   }

   // If serial is not faulty set it in normal operation mode
   // (not-loopback with IRQs enabled and OUT#1 and OUT#2 bits enabled)
   out8(COM1 + 4, 0x0F);
   return 0;
}

void serial_write(char c) {
	// Wait for empty transit
    while ((in8(COM1 + 5) & 0x20) == 0)
        ;
    out8(COM1, c);
}

bool serial_has_data() {
	return (in8(COM1 + 5) & 1) != 0;
}

u8 serial_read() {
	// Wait for 
	while (!serial_has_data())
		;

	return in8(COM1);
}

ssize_t tty_write(File *file, void *buf, size_t size)
{
	(void)file;
	u8 *p = buf;
	for (size_t i = 0; i < size; ++i)
		serial_write(p[i]);

	return size;
}

ssize_t tty_read(File *file, void *buf, size_t size)
{
	(void)file;
	u8 *p = buf;
	for (size_t i = 0; i < size; ++i)
		p[i] = serial_read();

	return size;
}

static FileOps tty_fops = {
	.open = fop_stub_open,
	.close = fop_stub_close,
	.write = tty_write,
	.read = tty_read,
	.seek = fop_generic_seek,
};

int tty_init()
{
	Device *dev = kzalloc(sizeof(Device));
	if (!dev)
		return -ENOMEM;
	dev->name = STR_LIT("tty0");
	return chrdev_create(dev, tty_fops);
}

void tty_exit()
{
}

BUILTIN_DRIVER("tty", tty_init, tty_exit);

