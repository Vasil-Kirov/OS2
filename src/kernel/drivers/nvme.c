#include <errno.h>
#include <pci.h>


int nvme_init()
{
	int bus;
	int dev;
	int fn;
	for(bus = 0; bus < 256; ++bus) {
		for(dev = 0; dev < 32; ++dev) {
			for(fn = 0; fn < 8; ++fn) {
				u32 read = pci_read(bus, dev, fn, 0x0);
				if((read & 0xFFFF) != 0xFFFF) {
					// Valid device
					read = pci_read(bus, dev, fn, 0x8);
					u8 class = read >> 24;
					u8 subclass = (read >> 16) & 0xFF;

					// Mass Storage,   NVMe
					if(class == 0x1 && subclass == 0x8)
						goto found_device;
				}
			}
		}
	}

	return -ENODEV;
found_device:

	return 0;
}





