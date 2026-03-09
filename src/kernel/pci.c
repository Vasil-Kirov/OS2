
#include "pci.h"

#include <string.h>
#include <kcommon.h>
#include <io.h>
#include <acpi.h>
#include <kmem.h>

#define CONFIG_ADDRESS (0xCF8)
#define CONFIG_DATA (0xCFC)


PCIe *pcie_init(RSDP *rsdp)
{
	PCIe *pcie = kmem_map(sizeof(*pcie));
	if(!pcie)
		return NULL;

	u32 mcfg_size;
	uintptr_t mcfg_phy_addr = rsdp_find_table(rsdp, "MCFG", &mcfg_size);
	if(!mcfg_phy_addr) {
		kmem_unmap(pcie);
		return NULL;
	}

	pcie->mcfg = kmem_map_phy_addr(mcfg_phy_addr, mcfg_size, PAGE_FLAG_RW);
	if(!pcie->mcfg) {
		kmem_unmap(pcie);
		return NULL;
	}

	pcie->entry_count = (mcfg_size - offsetof(MCFG, addrs)) / sizeof(MCFG_ConfigSpace);
	pcie->mapped_entry = -1;
	pcie->map = NULL;

	return pcie;
}

void pcie_unmap_config_space(PCIe *pcie)
{
	if(!pcie->map)
		return;
	kmem_unmap_raw(pcie->map, PCIE_SPACE_SIZE);
	pcie->mapped_entry = -1;
}

bool pcie_map_config_space(PCIe *pcie, u8 bus)
{
	if(pcie->map)
		pcie_unmap_config_space(pcie);

	for(size_t i = 0; i < pcie->entry_count; ++i) {
		if(pcie->mcfg->addrs[i].start_bus <= bus && pcie->mcfg->addrs[i].end_bus >= bus) {
			MCFG_ConfigSpace *cfg = &pcie->mcfg->addrs[i];
			pcie->map = kmem_map_phy_addr(cfg->base_addr, PCIE_SPACE_SIZE, PAGE_FLAG_RW);
			if(!pcie->map)
				return false;
			pcie->mapped_entry = i;
			return true;
		}
	}
	return false;
}

bool pcie_read(PCIe *pcie, u8 bus, u8 dev, u8 fn, void *buf, size_t size)
{
	if(!pcie)
		return false;

	for(size_t i = 0; i < pcie->entry_count; ++i) {
		if(pcie->mcfg->addrs[i].start_bus <= bus && pcie->mcfg->addrs[i].end_bus >= bus) {
			MCFG_ConfigSpace *cfg = &pcie->mcfg->addrs[i];
			void *mapped = NULL;
			if((int)i == pcie->mapped_entry) {
				mapped = pcie->map + (((bus-cfg->start_bus) << 20) | (dev << 15) | (fn << 12));
			} else {
				uintptr_t addr = cfg->base_addr + (((bus-cfg->start_bus) << 20) | (dev << 15) | (fn << 12));
				mapped = kmem_map_phy_addr(addr, size, PAGE_FLAG_RW);
			}

			if(!mapped)
				return false;

			memcpy(buf, mapped, size);

			if(i != pcie->mapped_entry)
				kmem_unmap_raw(mapped, size);

			return true;
		}
	}
	return false;
}

size_t pcie_bus_offset(u8 start_bus, u8 bus, u8 dev, u8 fn)
{
	return (((bus-start_bus) << 20) | (dev << 15) | (fn << 12));
}

u32 pci_read(u8 bus, u8 dev, u8 fn, u8 offset)
{
	u32 lbus  = bus;
	u32 ldev  = dev;
	u32 lfn   = fn;
	u32 address = (lbus << 16) | (ldev << 11) | (lfn << 8) | (offset & ~0b11) | (1 << 31);

	out32(CONFIG_ADDRESS, address);
	return in32(CONFIG_DATA);
}

