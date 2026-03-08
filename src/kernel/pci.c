
#include "pci.h"
#include "kcommon.h"
#include "io.h"

#define CONFIG_ADDRESS (0xCF8)
#define CONFIG_DATA (0xCFC)



u32 pci_read(u8 bus, u8 dev, u8 fn, u8 offset)
{
    u32 lbus  = bus;
    u32 ldev  = dev;
    u32 lfn   = fn;
    u32 address = (lbus << 16) | (ldev << 11) | (lfn << 8) | (offset & ~0b11) | (1 << 31);

	out32(CONFIG_ADDRESS, address);
	return in32(CONFIG_DATA);
}

