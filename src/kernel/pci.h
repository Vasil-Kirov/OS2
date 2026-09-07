
#ifndef _PCI_H
#define _PCI_H
#include <acpi.h>
#include <kcommon.h>
#include <drivers.h>

#define PCI_ANY_ID (~0u)

#define PCI_CMD_IO (1 << 0)
#define PCI_CMD_MEMORY_SPACE (1 << 1)
#define PCI_CMD_BUS_MASTER   (1 << 2)
#define PCI_CMD_INT_DISABLE  (1 << 10)

struct PCIe;
struct PCIeDriver;
struct PCIeDevice;

typedef struct {
	u32 class;
	u32 subclass;
	u32 vendor_id;
} PCIeDeviceID;

#define PCI_DEVICE_CLASS(class, subclass) \
	.class = (class), .subclass = (subclass), \
	.vendor_id = PCI_ANY_ID

typedef struct PCIeDevice {
	PCIeDeviceID id;
	int bus;
	int devn;
	int fn;
	Device dev;
	struct PCIe *prv;
	void *map;
	int cfg_space_idx;

	void *drv_data;
} PCIeDevice;

typedef struct PCIeDriver {
	PCIeDeviceID match;
	void *data;
	DeviceDriver drv;

	int (*probe)(struct PCIeDevice *pdev);
	void (*remove)(struct PCIeDevice *pdev);
} PCIeDriver;

int pci_register_driver(PCIeDriver *pdev);
int pci_enable(PCIeDevice *pdev);
int pci_set_command(PCIeDevice *pdev, u16 flags);
u32 pci_read_bar(PCIeDevice *pdev, u8 barn);

typedef struct PCIe {
	MCFG *mcfg;
	size_t entry_count;
	void *map;
	int mapped_entry;
} PCIe;

typedef struct {
	int bus;
	int dev;
	int fn;
	u16 vendor_id;
	u8 class;
	u8 subclass;
} PCIeDevDesc;

typedef bool (*PCIeItCallback)(PCIeDevDesc *dev, void *arg);
int pcie_iterate_entries(PCIe *pcie, PCIeItCallback cb, void *arg);
void pcie_register_devices(PCIe *pcie);
const char *pcie_class_name(u8 class);
const char *pcie_subclass_name(u8 class, u8 subclass);

PCIe *pcie_init(RSDP *rsdp);
bool pcie_map_config_space(PCIe *pcie, u8 bus);
void pcie_unmap_config_space(PCIe *pcie);
size_t pcie_bus_offset(u8 start_bus, u8 bus, u8 dev, u8 fn);
u32 pci_read(u8 bus, u8 dev, u8 fn, u8 offset);

// 256 Mb
#define PCIE_SPACE_SIZE (256 << 20)

#endif

