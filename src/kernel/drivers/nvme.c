#include <errno.h>
#include <pci.h>


int nvme_init(PCIe *pcie)
{
	int bus;
	int dev;
	int fn;
	for(size_t i = 0; i < pcie->entry_count; ++i) {
		MCFG_ConfigSpace *cfg = &pcie->mcfg->addrs[i];
		if(!pcie_map_config_space(pcie, cfg->start_bus))
			return -ENOMEM;
		
		for(bus = cfg->start_bus; bus <= cfg->end_bus; ++bus) {
			for(dev = 0; dev < 32; ++dev) {
				for(fn = 0; fn < 8; ++fn) {
					size_t space = pcie_bus_offset(cfg->start_bus, bus, dev, fn);
					u16 vendor_id = read16(pcie->map + space);

					// Invalid ID
					if(vendor_id == 0xFFFF)
						continue;

					u32 data = read32(pcie->map + space + 0x8);
					u8 class = data >> 24;
					u8 subclass = (data >> 16) & 0xFF;

					// Mass Storage,   NVMe
					if(class != 0x1 || subclass != 0x8)
						continue;
				}
			}
		}

		pcie_unmap_config_space(pcie);
	}

	return -ENODEV;
found_device:

	return 0;
}





