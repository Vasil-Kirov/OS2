#include "nvme.h"

#include <errno.h>
#include <pci.h>
#include <kcommon.h>
#include <kmem.h>

#define SQT_DRBL(dev, n) (0x1000 + (2*(n))   * (dev)->doorbell_stride)
#define CQH_DRBL(dev, n) (0x1000 + (2*(n)+1) * (dev)->doorbell_stride)

enum NVMeRegisters {
	NVMe_CAP = 0x00,
	NVMe_VS  = 0x08,
	NVMe_INTMS = 0x0C,
	NVMe_INTMC = 0x10,
	NVMe_CC = 0x14,
	NVMe_CSTS = 0x1C,
	NVMe_AQA  = 0x24,
	NVMe_ASQ  = 0x28,
	NVMe_ACQ  = 0x30,
};

enum NVMeAdminOp {
	NVMeAdmin_DeleteIOSQ = 0x00,
	NVMeAdmin_CreateIOSQ = 0x01,
	NVMeAdmin_GetLogPage = 0x02,
	NVMeAdmin_DeleteIOCQ = 0x04,
	NVMeAdmin_CreateIOCQ = 0x05,
	NVMeAdmin_Identify   = 0x06,
	NVMeAdmin_Abort      = 0x08,
};

enum NVMeNVMOp {
	NVMeNVM_Flush = 0x00,
	NVMeNVM_Write = 0x01,
	NVMeNVM_Read  = 0x02,
};

u32 nvme_read_reg32(NVMeDevice *nvme, int offset)
{
	return read32(nvme->base + offset);
}

void nvme_write_reg32(NVMeDevice *nvme, int offset, u32 dword)
{
	write32(nvme->base + offset, dword);
}

void nvme_reg32_clear(NVMeDevice *nvme, int offset, int bit)
{
	write32(nvme->base + offset, read32(nvme->base + offset) & ~(1 << bit));
}

void nvme_reg32_set(NVMeDevice *nvme, int offset, int bit)
{
	write32(nvme->base + offset, read32(nvme->base + offset) | (1 << bit));
}

u64 nvme_read_reg(NVMeDevice *nvme, int offset)
{
	return read64(nvme->base + offset);
}

void nvme_write_reg(NVMeDevice *nvme, int offset, u64 val)
{
	write64(nvme->base + offset, val);
}

u16 nvme_cmd(NVMeDevice *dev, NVMeQueue *q, NVMeSubmissionEntry *cmd)
{
	volatile NVMeSubmissionEntry *entry = q->sq + q->sq_tail;
	*entry = *cmd;
	q->sq_tail = (q->sq_tail + 1) % q->sq_size;
	nvme_write_reg32(dev, SQT_DRBL(dev, q->id), q->sq_tail);

	volatile NVMeCompletionEntry *comp = q->cq + q->cq_head;
	while(((comp->spc >> 16) & 1) != q->phase)
		;

	u16 status = comp->spc >> 17;

	q->cq_head = (q->cq_head + 1) % q->cq_size;
	if(q->cq_head == 0)
		q->phase ^= 1;
	nvme_write_reg32(dev, CQH_DRBL(dev, q->id), q->cq_head);
	return status;
}

static inline void nvme_set_prp(u32 *prp_lo, u32 *prp_hi, uintptr_t addr)
{
	*prp_lo = (u32)(addr & 0xFFFFFFFF);
	*prp_hi = (u32)((u64)addr >> 32);
}

bool nvme_write(NVMeDevice *dev, int nsid, u64 lba, u16 nblocks, uintptr_t buf_paddr)
{
	NVMeSubmissionEntry e = {};
	e.cmd = NVMeNVM_Write;
	e.NSID = nsid;
	nvme_set_prp(&e.prp[0], &e.prp[1], buf_paddr);
	e.arg[0] = lba & 0xFFFFFFFF;
	e.arg[1] = lba >> 32;
	e.arg[2] = (u16)(nblocks-1);
	u16 status = nvme_cmd(dev, &dev->io, &e);
	if (status != 0)
		return false;
	return true;
}

bool nvme_read(NVMeDevice *dev, int nsid, u64 lba, u16 nblocks, uintptr_t buf_paddr)
{
	NVMeSubmissionEntry e = {};
	e.cmd = NVMeNVM_Read;
	e.NSID = nsid;
	nvme_set_prp(&e.prp[0], &e.prp[1], buf_paddr);
	e.arg[0] = lba & 0xFFFFFFFF;
	e.arg[1] = lba >> 32;
	e.arg[2] = (u16)nblocks-1;
	u16 status = nvme_cmd(dev, &dev->io, &e);
	if (status != 0)
		return false;
	return true;
}

bool nvme_identify_namespace(NVMeDevice *dev, u32 nsid)
{
	void *vaddr;
	uintptr_t paddr;
	if (!dma_map(PAGE_SIZE, &vaddr, &paddr))
		return false;
	NVMeSubmissionEntry e = {};
	e.cmd = NVMeAdmin_Identify;
	e.NSID = nsid;
	nvme_set_prp(&e.prp[0], &e.prp[1], paddr);
	e.arg[0] = 0x0; // Identify namespace
	u16 status = nvme_cmd(dev, &dev->admin, &e);
	if (status != 0) {
		dma_unmap(paddr);
		return NULL;
	}

	memcpy(&dev->id_namespace, vaddr, sizeof(NVMeIdentifyNamespace));
	dma_unmap(paddr);
	return true;
}

bool nvme_identify_controller(NVMeDevice *dev)
{
	void *vaddr;
	uintptr_t paddr;
	if (!dma_map(PAGE_SIZE, &vaddr, &paddr))
		return NULL;
	NVMeSubmissionEntry e = {};
	e.cmd = NVMeAdmin_Identify;
	nvme_set_prp(&e.prp[0], &e.prp[1], paddr);
	e.arg[0] = 0x1; // Identify controller
	u16 status = nvme_cmd(dev, &dev->admin, &e);
	if (status != 0) {
		dma_unmap(paddr);
		return NULL;
	}

	memcpy(&dev->id_controller, vaddr, sizeof(NVMeIdentifyController));
	dma_unmap(paddr);
	return vaddr;
}

bool nvme_create_queue(NVMeQueue *q)
{
	void *svaddr;
	uintptr_t saddr;
	if(!dma_map(PAGE_SIZE, &svaddr, &saddr))
		return false;

	void *cvaddr;
	uintptr_t caddr;
	if(!dma_map(PAGE_SIZE, &cvaddr, &caddr)) {
		dma_unmap(saddr);
		return false;
	}
	q->sq = svaddr;
	q->sq_paddr = saddr;
	q->sq_size = PAGE_SIZE/sizeof(NVMeSubmissionEntry);
	q->sq_tail = 0;
	q->cq = cvaddr;
	q->cq_paddr = caddr;
	q->cq_size = PAGE_SIZE/(sizeof(NVMeCompletionEntry)*4);
	q->cq_head = 0;
	q->phase = 1;
	return true;
}

bool nvme_create_admin_queue(NVMeDevice *dev, NVMeQueue *q)
{
	if (!nvme_create_queue(q))
		return false;
	q->id = 0;
	nvme_write_reg(dev, 0x28, (u64)q->sq_paddr);
	nvme_write_reg(dev, 0x30, (u64)q->cq_paddr);
	return true;
}

bool nvme_create_io_queue(NVMeDevice *dev, NVMeQueue *admin, NVMeQueue *q, int qid)
{
	if (!nvme_create_queue(q))
		return false;
	q->id = qid;
	NVMeSubmissionEntry e0 = {};
	e0.cmd = NVMeAdmin_CreateIOCQ;
	nvme_set_prp(&e0.prp[0], &e0.prp[1], q->cq_paddr);
	e0.arg[0] = qid | ((q->cq_size-1) << 16); // 0 based for some reason
	e0.arg[1] = 1; // Physically contigous
	u16 status = nvme_cmd(dev, admin, &e0);
	if (status != 0)
		panic("Failed to create NVMe io completion queue!");

	NVMeSubmissionEntry e1 = {};
	e1.cmd = NVMeAdmin_CreateIOSQ;
	nvme_set_prp(&e1.prp[0], &e1.prp[1], q->sq_paddr);
	e1.arg[0] = qid | ((q->sq_size-1) << 16); // 0 based for some reason
	e1.arg[1] = 1 | qid << 16; // Physically contigous, completion q is qid
	status = nvme_cmd(dev, admin, &e1);
	if (status != 0)
		panic("Failed to create NVMe io submission queue!");

	return true;
}

int nvme_init(PCIe *pcie, NVMeDevice *dev)
{
	int bus;
	int devn;
	int fn;
	MCFG_ConfigSpace *cfg = NULL;
	for(size_t i = 0; i < pcie->entry_count; ++i) {
		cfg = &pcie->mcfg->addrs[i];
		if(!pcie_map_config_space(pcie, cfg->start_bus))
			return -ENOMEM;
		
		for(bus = cfg->start_bus; bus <= cfg->end_bus; ++bus) {
			for(devn = 0; devn < 32; ++devn) {
				for(fn = 0; fn < 8; ++fn) {
					size_t space = pcie_bus_offset(cfg->start_bus, bus, devn, fn);
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

					goto found_device;
				}
			}
		}

		pcie_unmap_config_space(pcie);
	}
	return -ENODEV;

found_device:
	int err = 0;

	size_t space = pcie_bus_offset(cfg->start_bus, bus, devn, fn);
	u32 status_cmd = read32(pcie->map + space + 0x04);
	status_cmd |= (1 << 2) | (1 << 1); // Enable Bus Mastering and Memory Space Access
	status_cmd &= ~(1 << 10); // Enable interupts
	write32(pcie->map + space + 0x04, status_cmd);

	u32 bar0 = read32(pcie->map + space + 0x10) & ~0b1111;
	u32 bar1 = read32(pcie->map + space + 0x14) & ~0b1111;
	if (bar1) {
		err = -EFAULT;
		goto error_unmap_config;
	}

	void *base = kmem_map_phy_addr(bar0, PAGE_SIZE*2, PAGE_FLAG_MMIO);
	if (base == NULL) {
		err = -ENOMEM;
		goto error_unmap_config;
	}

	*dev = (NVMeDevice){};
	dev->base = base;
	
	nvme_reg32_clear(dev, NVMe_CC, 0); // CC.EN = 0
	while ((nvme_read_reg32(dev, NVMe_CSTS) & 1) != 0) ;

	u64 CAP = nvme_read_reg(dev, NVMe_CAP);
	dev->doorbell_stride = 4 <<  ((CAP >> 32) & 0xF);

	u8 mpsmin = (CAP >> 48) & 0xF;
	if (mpsmin != 0) {
		err = -ENODEV;
		goto error_free_base;
	}

	u8 CSS = (CAP >> 37) & 0xFF;
	if ((CSS & 1) == 0) { // No NVM command set
		err = -ENODEV;
		goto error_free_base;
	}
	u32 CC = nvme_read_reg32(dev, NVMe_CC);
	CC &= ~(0x7 << 4); // CSS = NVM Command Set
	CC &= ~(0xF << 7); // MPS = 4Kb
	CC &= ~(0x7 << 11); // AMS = Round Robin (Supported on all devices)

	CC &= ~(0xF << 16);
	CC |= 0x6 << 16; // IOSQES (IO Submission Queue Entry Size) = 2^6(64)
	CC &= ~(0xF << 20);
	CC |= 0x4 << 20; // IOSQES (IO Completion Queue Entry Size) = 2^4(16)

	nvme_write_reg32(dev, NVMe_CC, CC);


	if (!nvme_create_admin_queue(dev, &dev->admin)) {
		err = -ENOMEM;
		goto error_free_base;
	}

	u32 AQA =  ((dev->admin.sq_size-1) & 0xFFF) << 0;
	AQA |= ((dev->admin.cq_size-1) & 0xFFF) << 16;
	nvme_write_reg32(dev, NVMe_AQA, AQA);

	nvme_reg32_set(dev, NVMe_CC, 0); // CC.EN = 1
	while ((nvme_read_reg32(dev, NVMe_CSTS) & 1) == 0) ;

	if (!nvme_create_io_queue(dev, &dev->admin, &dev->io, 1)) {
		err = -ENOMEM;
		goto error_free_admin_queue;
	}

	if (!nvme_identify_controller(dev)) {
		err = -ENOMEM;
		goto error_free_io_queue;
	}

	dev->ns_count = 0;
	for (u32 i = 0; i < dev->id_controller.NN; ++i)
	{
		if (!nvme_identify_namespace(dev, i+1))
			continue;
		if (dev->id_namespace.NSZE == 0)
			continue;

		u8 fidx = dev->id_namespace.FLBAS & 0xF;
		u32 block_size = 1 << dev->id_namespace.lbaf[fidx].LBADS;

		dev->ns_infos[dev->ns_count++] = (NVMeNamespaceInfo){
			.nsid = i+1,
			.num_blocks = dev->id_namespace.NSZE,
			.block_size = block_size,
		};
	}
	if (dev->ns_count == 0) {
		err = -ENODEV;
		goto error_free_io_queue;
	}

	return 0;

error_free_io_queue:
	dma_unmap(dev->io.sq_paddr);
	dma_unmap(dev->io.cq_paddr);

error_free_admin_queue:
	dma_unmap(dev->admin.sq_paddr);
	dma_unmap(dev->admin.cq_paddr);

error_free_base:
	kmem_unmap(base);

error_unmap_config:
	pcie_unmap_config_space(pcie);
	return err;
}

