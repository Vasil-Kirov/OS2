
#ifndef _DRIVERS_H
#define _DRIVERS_H

#include <vfs.h>
#include <kcommon.h>
#include <list.h>

struct Device;
struct DeviceDriver;

typedef struct {
	ListNode bus_list;
	struct DeviceDriver *drv;
} DriverPrivate;

typedef struct {
	int (*match)(struct Device *dev, const struct DeviceDriver *drv);
	int (*probe)(struct Device *dev);
	void (*remove)(struct Device *dev);
	ListNode devices;
	ListNode drivers;
} DeviceBus;

enum DevResType {
	DevRes_Stub,
	DevRes_Malloc,
	DevRes_MMIO,
	DevRes_DMA,
};

typedef struct {
	enum DevResType kind;
	union {
		void *malloc_mem;
		struct {
			uintptr_t phy_addr;
			void *virt_addr;
		} dma;
		struct {
			void *mem;
			size_t size;
		} mmio;
	};
	ListNode node;
} DeviceRes;

typedef struct DeviceDriver {
	string_view name;
	DeviceBus *bus;
	DriverPrivate *prv;
} DeviceDriver;

typedef struct Device {
	string_view name;
	string_view base_name; // set by device_register before giving indexed number
	DeviceDriver *drv;
	DeviceBus *bus;
	ListNode resources;
	ListNode bus_node;
	void *data;

	char name_buf[64];
} Device;

typedef struct {
	const char *name;
	int (*init)();
	void (*exit)();
} DriverInvokeData;

#define BUILTIN_DRIVER(name, initfn, exitfn) \
	static const DriverInvokeData __driver_##initfn \
	__attribute__((section(".drivers"), used)) = {     \
		name, initfn, exitfn                           \
	};

int drivers_init();
void drivers_deinit();
int device_register(Device *dev);
void device_free_resources(Device *dev);
int driver_register(DeviceDriver *drv);
int bus_register(DeviceBus *bus);
bool devm_dma_map(Device *dev, size_t size, void **vaddr, uintptr_t *paddr);
void devm_dma_unmap(Device *dev, void *vaddr);
void *devm_kzalloc(Device *dev, size_t size);
void *devm_map_mmio(Device *dev, uintptr_t addr, size_t size);

int chrdev_create(Device *dev, FileOps fops);

#endif

