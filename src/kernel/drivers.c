#include "drivers.h"
#include <vfs.h>
#include <kmem.h>
#include <kprintf.h>
#include <kcommon.h>
#include <list.h>
#include <kmalloc.h>
#include <errno.h>

extern const DriverInvokeData __drivers_start[];
extern const DriverInvokeData __drivers_end[];

typedef struct {
	const DriverInvokeData *drv;
	ListNode node;
} ManagedDriver;

static ListNode driver_list = {};

int drivers_init()
{
	list_init(&driver_list);
    for (const DriverInvokeData *d = __drivers_start; d < __drivers_end; d++) {
		ManagedDriver *drv = kmalloc(sizeof(ManagedDriver));
		if (!drv) {
			ManagedDriver *it;
			list_for_each_entry(it, &driver_list, node) {
				kfree(it);
			}
			return -ENOMEM;
		}
		list_init(&drv->node);
		int r = d->init();
		if (r == 0) {
			list_add(&driver_list, &drv->node);
		} else {
			kprintf("Driver %s: Init failed with %d", d->name, r);
		}
	}
	return 0;
}

void drivers_deinit()
{
	ManagedDriver *it;
	ManagedDriver *n;
	list_for_each_entry_safe(it, n, &driver_list, node) {
		it->drv->exit();
		list_remove(&it->node);
		kfree(it);
	}
}

int chrdev_create(Device *dev, FileOps fops)
{
	DirEntry *devdir = vfs_find(STR_LIT("/dev"));
	if (IS_ERR_OR_NULL(devdir)) {
		return PTR_ERR_OR(devdir, -ENOENT);
	}
	DirEntry *deve = devdir->inode->op.create(devdir, &dev->name, S_IFCHR | 0777);
	if (IS_ERR_OR_NULL(deve))
		return PTR_ERR_OR(deve, -EIO);

	deve->inode->fop = fops;
	deve->inode->priv = dev;

	return 0;
}

int bus_register(DeviceBus *bus)
{
	if (!bus->match || !bus->probe || !bus->remove)
		return -EINVAL;

	list_init(&bus->drivers);
	list_init(&bus->devices);
	return 0;
}

int driver_register(DeviceDriver *drv)
{
	if (!drv || !drv->bus)
		return -EINVAL;

	DriverPrivate *prv = kzalloc(sizeof(DriverPrivate));
	if (!prv)
		return -ENOMEM;
	list_init(&prv->bus_list);

	drv->prv = prv;
	prv->drv = drv;
	list_add(&drv->bus->drivers, &prv->bus_list);

	return 0;
}

int device_register(Device *dev)
{
	DeviceBus *bus = dev->bus;
	if (bus == NULL || !bus->match) {
		kprintf("Device %[str]: Trying to register a device without a valid bus.", dev->name);
		return -EINVAL;
	}

	if (dev->resources.next && !list_empty(&dev->resources)) {
		kprintf("Device %[str]: Has resources attached before being registered.", dev->name);
		return -EINVAL;
	}

	list_init(&dev->resources);
	list_init(&dev->bus_node);
	int dev_idx = 0;

	Device *devit;
	list_for_each_entry(devit, &bus->devices, bus_node) {
		if (str_compare(&devit->base_name, &dev->name)) {
			dev_idx++;
		}
	}
	dev->base_name = dev->name;
	snprintf(dev->name_buf, ARRAY_COUNT(dev->name_buf), "%[str]%d", dev->name, dev_idx);
	dev->name = str_cstr(dev->name_buf);

	int ret = -ENODEV;
	DriverPrivate *it;
	list_for_each_entry(it, &bus->drivers, bus_list) {
		int r = bus->match(dev, it->drv);
		if (r < 0) {
			kprintf("Match during registration for %[str] failed with %d.", dev->name, r);
			return r;
		} else if (r != 0) {
			dev->drv = it->drv;
			r = bus->probe(dev);
			if (r != 0) {
				kprintf("Failed probe driver %[str] for device %[str].", it->drv->name, dev->name);
				device_free_resources(dev);
				dev->drv = NULL;
				continue;
			}

			ret = 0;
			break;
		}
	}

	list_add(&bus->devices, &dev->bus_node);
	return ret;
}

void device_unregister(Device *dev)
{
	if (dev->drv && dev->bus) {
		dev->bus->remove(dev);
	}
	device_free_resources(dev);
}

void device_free_resources(Device *dev)
{
	DeviceRes *it, *n;
	list_for_each_entry_safe(it, n, &dev->resources, node) {
		switch (it->kind) {
			case DevRes_DMA:
			{
				dma_unmap(it->dma.virt_addr);
			} break;
			case DevRes_Malloc:
			{
				kfree(it->malloc_mem);
			} break;
			case DevRes_MMIO:
			{
				kmem_unmap_raw(it->mmio.mem, it->mmio.size);
			} break;
			case DevRes_Stub:
			{
			} break;
		}
		list_remove(&it->node);
		kfree(it);
	}
}

void *devm_map_mmio(Device *dev, uintptr_t addr, size_t size)
{
	if (!dev || addr == 0 || size == 0)
		return NULL;

	DeviceRes *res = kzalloc(sizeof(DeviceRes));
	if (!res)
		return NULL;
	list_init(&res->node);

	void *mem = kmem_map_phy_addr(addr, size, PAGE_FLAG_MMIO);
	if (!mem) {
		kfree(res);
		return NULL;
	}

	res->kind = DevRes_MMIO;
	res->mmio.mem = mem;
	res->mmio.size = size;
	list_add(&dev->resources, &res->node);

	return mem;
}

bool devm_dma_map(Device *dev, size_t size, void **vaddr, uintptr_t *paddr)
{
	if (!vaddr || !paddr || !dev)
		return false;

	DeviceRes *res = kzalloc(sizeof(DeviceRes));
	if (!res)
		return false;
	list_init(&res->node);

	if (!dma_map(size, vaddr, paddr)) {
		kfree(res);
		return false;
	}
	res->kind = DevRes_DMA;
	res->dma.phy_addr = *paddr;
	res->dma.virt_addr = *vaddr;

	list_add(&dev->resources, &res->node);

	return true;
}

void devm_dma_unmap(Device *dev, void *vaddr)
{
	DeviceRes *it, *n;
	list_for_each_entry_safe(it, n, &dev->resources, node) {
		if (it->kind == DevRes_DMA && it->dma.virt_addr == vaddr) {
			dma_unmap(vaddr);
			kfree(it);
			list_remove(&it->node);

			return;
		}
	}
}

void *devm_kzalloc(Device *dev, size_t size)
{
	if (!dev)
		return NULL;

	DeviceRes *res = kzalloc(sizeof(DeviceRes));
	if (!res)
		return NULL;
	list_init(&res->node);

	void *p = kzalloc(size);
	if (!p) {
		kfree(res);
		return NULL;
	}

	res->malloc_mem = p;
	list_add(&dev->resources, &res->node);
	return p;
}

