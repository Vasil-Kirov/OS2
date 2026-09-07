#include "pci.h"

#include <kprintf.h>
#include <kmalloc.h>
#include <string.h>
#include <kcommon.h>
#include <io.h>
#include <acpi.h>
#include <kmem.h>
#include <errno.h>

#define CONFIG_ADDRESS (0xCF8)
#define CONFIG_DATA (0xCFC)

int pci_enable(PCIeDevice *pdev)
{
	if (!pdev)
		return -EINVAL;
	if (pdev->map)
		return 0;
	PCIe *pcie = pdev->prv;
	for(size_t i = 0; i < pcie->entry_count; ++i) {
		MCFG_ConfigSpace *cfg = &pcie->mcfg->addrs[i];
		if(cfg->start_bus <= pdev->bus && cfg->end_bus >= pdev->bus) {

			uintptr_t addr = cfg->base_addr + pcie_bus_offset(cfg->start_bus, pdev->bus, pdev->devn, pdev->fn);
			pdev->map = kmem_map_phy_addr(addr, KB(4), PAGE_FLAG_MMIO);
			if (pdev->map == NULL)
				return -ENOMEM;
			pdev->cfg_space_idx = i;
			return 0;
		}
	}
	return -ENODEV;
}

int pci_set_command(PCIeDevice *pdev, u16 flags)
{
	if (!pdev)
		return -EINVAL;
	if (!pdev->map || pdev->cfg_space_idx < 0)
		return -EINVAL;

	PCIe *pcie = pdev->prv;
	MCFG_ConfigSpace *cfg = &pcie->mcfg->addrs[pdev->cfg_space_idx];
	size_t space = pcie_bus_offset(cfg->start_bus, pdev->bus, pdev->devn, pdev->fn);
	u32 status_cmd = read32(pcie->map + space + 0x04);
	status_cmd &= ~0x3FF;
	status_cmd |= flags & 0x3FF;
	write32(pcie->map + space + 0x04, status_cmd);
	return 0;
}

u32 pci_read_bar(PCIeDevice *pdev, u8 barn)
{
	if (barn > 5)
		return ~0;
	if (!pdev)
		return ~0;
	if (!pdev->map || pdev->cfg_space_idx < 0)
		return ~0;
	PCIe *pcie = pdev->prv;
	MCFG_ConfigSpace *cfg = &pcie->mcfg->addrs[pdev->cfg_space_idx];
	size_t space = pcie_bus_offset(cfg->start_bus, pdev->bus, pdev->devn, pdev->fn);
	u32 bar = read32(pcie->map + space + 0x10 + (barn*4));
	return bar;
}

static bool pcie_match_id(u32 id_a, u32 id_b)
{
	if (id_a != PCI_ANY_ID && id_b != PCI_ANY_ID && id_a != id_b)
		return false;
	return true;
}

int pcie_match(struct Device *dev_, const struct DeviceDriver *drv_)
{
	if (!dev_ || !drv_)
		return -EINVAL;

	PCIeDevice *dev = container_of(dev_, PCIeDevice, dev);
	const PCIeDriver *drv = container_of(drv_, PCIeDriver, drv);
	if (!pcie_match_id(drv->match.class, dev->id.class))
		return 0;
	if (!pcie_match_id(drv->match.subclass, dev->id.subclass))
		return 0;
	if (!pcie_match_id(drv->match.vendor_id, dev->id.vendor_id))
		return 0;

	return 1;
}

static int pcie_probe(struct Device *dev_)
{
	if (!dev_->drv)
		return -EINVAL;

	PCIeDevice *dev = container_of(dev_, PCIeDevice, dev);
	PCIeDriver *drv = container_of(dev_->drv, PCIeDriver, drv);

	return drv->probe(dev);
}

static void pcie_remove(struct Device *dev_)
{
	if (!dev_->drv)
		return;

	PCIeDevice *dev = container_of(dev_, PCIeDevice, dev);
	PCIeDriver *drv = container_of(dev_->drv, PCIeDriver, drv);

	drv->remove(dev);
}

DeviceBus pcie_bus = {
	.match = pcie_match,
	.probe = pcie_probe,
	.remove = pcie_remove,
};

PCIe *pcie_init(RSDP *rsdp)
{
	int err = bus_register(&pcie_bus);
	if (err != 0) {
		kprintf("Failed to register PCIe bus, error %d", err);
		return NULL;
	}

	PCIe *pcie = kmem_map(sizeof(*pcie), PAGE_FLAG_RW);
	if(!pcie)
		return NULL;

	u32 mcfg_size;
	uintptr_t mcfg_phy_addr = rsdp_find_table(rsdp, "MCFG", &mcfg_size);
	if(!mcfg_phy_addr) {
		kmem_unmap(pcie);
		return NULL;
	}

	pcie->mcfg = kmem_map_phy_addr(mcfg_phy_addr, mcfg_size, PAGE_FLAG_MMIO);
	if(!pcie->mcfg) {
		kmem_unmap(pcie);
		return NULL;
	}

	pcie->entry_count = (mcfg_size - offsetof(MCFG, addrs)) / sizeof(MCFG_ConfigSpace);
	pcie->mapped_entry = -1;
	pcie->map = NULL;

	return pcie;
}

const char *pcie_class_name(u8 class)
{
	switch (class)
	{
		case 0x00:
			return "Unclassified";
		case 0x01:
			return "Mass Storage Controller";
		case 0x02:
			return "Network Controller";
		case 0x03:
			return "Display Controller";
		case 0x04:
			return "Multimedia Controller";
		case 0x05:
			return "Memory Controller";
		case 0x06:
			return "Bridge Device";
		case 0x07:
			return "Simple Communication Controller";
		case 0x08:
			return "Base System Peripheral";
		case 0x09:
			return "Input Device Controller";
		case 0x0A:
			return "Docking Station";
		case 0x0B:
			return "Processor";
		case 0x0C:
			return "Serial Bus Controller";
		case 0x0D:
			return "Wireless Controller";
		case 0x0E:
			return "Intelligent I/O Controller";
		case 0x0F:
			return "Satellite Communication Controller";
		case 0x10:
			return "Encryption Controller";
		case 0x11:
			return "Signal Processing Controller";
		case 0x12:
			return "Processing Accelerator";
		case 0x13:
			return "Non-Essential Instrumentation";
		case 0x40:
			return "Co-Processor";
		default:
			return "Unknown";
	}
}

const char *pcie_subclass_name(u8 class, u8 subclass)
{
	switch (class)
	{
		case 0x01: // Mass Storage Controller
			switch (subclass)
			{
				case 0x00: return "SCSI";
				case 0x01: return "IDE";
				case 0x02: return "Floppy Disk Controller";
				case 0x03: return "IPI Bus Controller";
				case 0x04: return "RAID Controller";
				case 0x05: return "ATA Controller";
				case 0x06: return "SATA Controller";
				case 0x07: return "Serial Attached SCSI";
				case 0x08: return "NVM Express";
				case 0x80: return "Other";
				default:   return "Unknown";
			}

		case 0x02: // Network Controller
			switch (subclass)
			{
				case 0x00: return "Ethernet Controller";
				case 0x01: return "Token Ring Controller";
				case 0x02: return "FDDI Controller";
				case 0x03: return "ATM Controller";
				case 0x04: return "ISDN Controller";
				case 0x05: return "WorldFIP Controller";
				case 0x06: return "PICMG 2.14 Multi Computing";
				case 0x80: return "Other";
				default:   return "Unknown";
			}

		case 0x03: // Display Controller
			switch (subclass)
			{
				case 0x00: return "VGA Compatible Controller";
				case 0x01: return "XGA Controller";
				case 0x02: return "3D Controller";
				case 0x80: return "Other";
				default:   return "Unknown";
			}

		case 0x06: // Bridge Device
			switch (subclass)
			{
				case 0x00: return "Host Bridge";
				case 0x01: return "ISA Bridge";
				case 0x02: return "EISA Bridge";
				case 0x03: return "MCA Bridge";
				case 0x04: return "PCI-to-PCI Bridge";
				case 0x05: return "PCMCIA Bridge";
				case 0x06: return "NuBus Bridge";
				case 0x07: return "CardBus Bridge";
				case 0x08: return "RACEway Bridge";
				case 0x09: return "PCI-to-PCI Bridge (Semi-transparent)";
				case 0x0A: return "InfiniBand-to-PCI Host Bridge";
				case 0x80: return "Other";
				default:   return "Unknown";
			}

		case 0x0C: // Serial Bus Controller
			switch (subclass)
			{
				case 0x00: return "FireWire";
				case 0x01: return "ACCESS Bus";
				case 0x02: return "SSA";
				case 0x03: return "USB Controller";
				case 0x04: return "Fibre Channel";
				case 0x05: return "SMBus";
				case 0x06: return "InfiniBand";
				case 0x07: return "IPMI";
				case 0x08: return "SERCOS Interface";
				case 0x09: return "CANbus";
				case 0x80: return "Other";
				default:   return "Unknown";
			}

		default:
			return "Unknown";
	} }

static const char *pcie_device_prefix(u8 class, u8 subclass)
{
	switch (class) {
		case 0x01: // Mass Storage Controller
		switch (subclass) {
			case 0x00: return "sd";   // SCSI
			case 0x01: return "hd";   // IDE
			case 0x02: return "fd";   // Floppy
			case 0x04: return "md";   // RAID
			case 0x05: return "sd";   // ATA
			case 0x06: return "sd";   // SATA
			case 0x07: return "sd";   // SAS
			case 0x08: return "nvme"; // NVMe
			default:   return "sd";
		}
		case 0x02: // Network Controller
		switch (subclass) {
			case 0x00: return "eth"; // Ethernet
			default:   return "net";
		}
		case 0x03: // Display Controller
		return "fb";
		case 0x0C: // Serial Bus Controller
		switch (subclass) {
			case 0x00: return "fw";   // FireWire
			case 0x03: return "usb";  // USB
			default:   return "sbus";
		}
		default:
		return "dev";
	}
}

static bool pcie_register_callback(PCIeDevDesc *desc, void *pcie)
{
	PCIeDevice *dev = kzalloc(sizeof(PCIeDevice));
	if (!dev)
		return true;

	const char *devname = pcie_device_prefix(desc->class, desc->subclass);
	const char *readable_name = pcie_subclass_name(desc->class, desc->subclass);


	dev->cfg_space_idx = -1;
	dev->prv = pcie;
	dev->id.class = desc->class;
	dev->id.subclass = desc->subclass;
	dev->id.vendor_id = desc->vendor_id;
	dev->bus = desc->bus;
	dev->devn = desc->dev;
	dev->fn = desc->fn;

	dev->dev.name = str_cstr(devname);
	dev->dev.bus = &pcie_bus;
	int res = device_register(&dev->dev);

	size_t name_len = strlen(readable_name);
	char pad[128] = {};
	for (size_t i = 0; i < ARRAY_COUNT(pad); ++i) {
		if (name_len+i > 32)
			break;
		pad[i] = ' ';
	}

	if (res == 0) {
		kprintf("\t%s%s : bound to /dev/%[str]", readable_name, pad, dev->dev.name);
	} else if (res == -ENODEV) {
		kprintf("\t%s%s : no matching driver", readable_name, pad);
	} else {
		kprintf("\t%s%s : error %d", readable_name, pad, res);
	}
	return false;
}

void pcie_register_devices(PCIe *pcie)
{
	kprintf("PCI: Registering devices");
	pcie_iterate_entries(pcie, pcie_register_callback, pcie);
}

int pcie_iterate_entries(PCIe *pcie, PCIeItCallback cb, void *arg)
{
	if (!cb)
		return -EINVAL;

	MCFG_ConfigSpace *cfg = NULL;
	for (size_t i = 0; i < pcie->entry_count; ++i) {
		cfg = &pcie->mcfg->addrs[i];
		if (!pcie_map_config_space(pcie, cfg->start_bus))
			return -ENOMEM;

		for (int bus = cfg->start_bus; bus <= cfg->end_bus; ++bus) {
			for(int dev = 0; dev < 32; ++dev) {
				for(int fn = 0; fn < 8; ++fn) {
					size_t space = pcie_bus_offset(cfg->start_bus, bus, dev, fn);
					u16 vendor_id = read16(pcie->map + space);

					// Invalid ID
					if(vendor_id == 0xFFFF)
						continue;

					u32 data = read32(pcie->map + space + 0x8);
					u8 class = data >> 24;
					u8 subclass = (data >> 16) & 0xFF;

					PCIeDevDesc desc = {bus, dev, fn, vendor_id, class, subclass}; 
					if (cb(&desc, arg))
						goto early_exit;
				}
			}
		}

		pcie_unmap_config_space(pcie);
	}
	return 0;

early_exit:
	pcie_unmap_config_space(pcie);
	return 0;
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
			pcie->map = kmem_map_phy_addr(cfg->base_addr, PCIE_SPACE_SIZE, PAGE_FLAG_MMIO);
			if(!pcie->map)
				return false;
			pcie->mapped_entry = (int)i;
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
				mapped = kmem_map_phy_addr(addr, size, PAGE_FLAG_MMIO);
			}

			if(!mapped)
				return false;

			memcpy(buf, mapped, size);

			if((int)i != pcie->mapped_entry)
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


int pci_register_driver(PCIeDriver *drv)
{
	drv->drv.bus = &pcie_bus;
	return driver_register(&drv->drv);
}

