
#ifndef _PCI_H
#define _PCI_H
#include <acpi.h>
#include <kcommon.h>

typedef struct {
	MCFG *mcfg;
	size_t entry_count;
	void *map;
	int mapped_entry;
} PCIe;

PCIe *pcie_init(RSDP *rsdp);
bool pcie_map_config_space(PCIe *pcie, u8 bus);
void pcie_unmap_config_space(PCIe *pcie);
size_t pcie_bus_offset(u8 start_bus, u8 bus, u8 dev, u8 fn);
u32 pci_read(u8 bus, u8 dev, u8 fn, u8 offset);

// 256 Mb
#define PCIE_SPACE_SIZE (256 << 20)

#endif

