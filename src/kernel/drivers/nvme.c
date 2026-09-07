#include "nvme.h"

#include <block.h>
#include <kprintf.h>
#include <drivers.h>
#include <kmalloc.h>
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

bool nvme_identify_namespace(NVMeDevice *ndev, u32 nsid)
{
	void *vaddr;
	uintptr_t paddr;
	if (!dma_map(PAGE_SIZE, &vaddr, &paddr))
		return false;
	memset(vaddr, 0, PAGE_SIZE);

	NVMeSubmissionEntry e = {};
	e.cmd = NVMeAdmin_Identify;
	e.NSID = nsid;
	nvme_set_prp(&e.prp[0], &e.prp[1], paddr);
	e.arg[0] = 0x0; // Identify namespace
	u16 status = nvme_cmd(ndev, &ndev->admin, &e);
	if (status != 0) {
		dma_unmap(vaddr);
		return NULL;
	}

	memcpy(&ndev->id_namespace, vaddr, sizeof(NVMeIdentifyNamespace));
	dma_unmap(vaddr);
	return true;
}

bool nvme_identify_controller(NVMeDevice *ndev)
{
	void *vaddr;
	uintptr_t paddr;
	if (!dma_map(PAGE_SIZE, &vaddr, &paddr))
		return NULL;
	memset(vaddr, 0, PAGE_SIZE);

	NVMeSubmissionEntry e = {};
	e.cmd = NVMeAdmin_Identify;
	nvme_set_prp(&e.prp[0], &e.prp[1], paddr);
	e.arg[0] = 0x1; // Identify controller
	u16 status = nvme_cmd(ndev, &ndev->admin, &e);
	if (status != 0) {
		dma_unmap(vaddr);
		return NULL;
	}

	memcpy(&ndev->id_controller, vaddr, sizeof(NVMeIdentifyController));
	dma_unmap(vaddr);
	return vaddr;
}

bool nvme_create_queue(NVMeDevice *ndev, NVMeQueue *q)
{
	void *svaddr;
	uintptr_t saddr;
	if(!devm_dma_map(&ndev->pdev->dev, PAGE_SIZE, &svaddr, &saddr))
		return false;

	void *cvaddr;
	uintptr_t caddr;
	if(!devm_dma_map(&ndev->pdev->dev, PAGE_SIZE, &cvaddr, &caddr)) {
		devm_dma_unmap(&ndev->pdev->dev, svaddr);
		return false;
	}
	memset(svaddr, 0, PAGE_SIZE);
	memset(cvaddr, 0, PAGE_SIZE);

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

bool nvme_create_admin_queue(NVMeDevice *ndev, NVMeQueue *q)
{
	if (!nvme_create_queue(ndev, q))
		return false;
	q->id = 0;
	nvme_write_reg(ndev, 0x28, (u64)q->sq_paddr);
	nvme_write_reg(ndev, 0x30, (u64)q->cq_paddr);
	return true;
}

bool nvme_create_io_queue(NVMeDevice *dev, NVMeQueue *admin, NVMeQueue *q, int qid)
{
	if (!nvme_create_queue(dev, q))
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

int nvme_block_write(BlockDevice *blk, u64 block_idx, u16 block_count, uintptr_t buf)
{
	NVMeDevice *dev = blk->data;
	if (!nvme_write(dev, 1, block_idx, block_count, buf))
		return -EIO;
	return 0;
}

int nvme_block_read(BlockDevice *blk, u64 block_idx, u16 block_count, uintptr_t buf)
{
	NVMeDevice *dev = blk->data;
	if (!nvme_read(dev, 1, block_idx, block_count, buf))
		return -EIO;
	return 0;
}

#define NVME_POLL_MAX  10000000u
static int nvme_wait_ready(NVMeDevice *ndev, u32 want)
{
	for (u32 i = 0; i < NVME_POLL_MAX; ++i) {
		u32 csts = nvme_read_reg32(ndev, NVMe_CSTS);
		if (csts & 2) // Controller Fatal Status
			return -EIO;
		if ((csts & 1) == want)
			return 0;
	}
	return -ETIMEDOUT;
}

int nvme_probe(PCIeDevice *pdev)
{
	int err = 0;

	err = pci_enable(pdev);
	if (err != 0)
		return err;

	// Enable Bus Mastering and Memory Space Access
	pci_set_command(pdev, PCI_CMD_MEMORY_SPACE | PCI_CMD_BUS_MASTER | PCI_CMD_INT_DISABLE);
	u32 bar0v = pci_read_bar(pdev, 0);
	u32 bar1v = pci_read_bar(pdev, 1);
	if (bar0v == ~0u || bar1v == ~0u)
		return -EIO;

	u32 bar0 = bar0v & ~0b1111;
	u32 bar1 = bar1v & ~0b1111;
	if (bar1)
		return -EFAULT;

	void *base = devm_map_mmio(&pdev->dev, bar0, PAGE_SIZE*2);
	if (base == NULL)
		return -ENOMEM;

	NVMeDevice *ndev = devm_kzalloc(&pdev->dev, sizeof(NVMeDevice));
	if (!ndev)
		return -ENOMEM;

	pdev->drv_data = ndev;
	ndev->base = base;
	ndev->pdev = pdev;
	
	u32 CC = nvme_read_reg32(ndev, NVMe_CC);
	if (CC & 1) { // CC.EN == 1
		err = nvme_wait_ready(ndev, 1); // Wait for CS.RDY == 1
		if (err != 0)
			return err;

		nvme_write_reg32(ndev, NVMe_CC, CC & ~1); // CC.EN = 0
	}
	err = nvme_wait_ready(ndev, 0);
	if (err != 0)
		return err;

	u64 CAP = nvme_read_reg(ndev, NVMe_CAP);
	ndev->doorbell_stride = 4 <<  ((CAP >> 32) & 0xF);

	u8 mpsmin = (CAP >> 48) & 0xF;
	if (mpsmin != 0) {
		return -ENODEV;
	}

	u8 CSS = (CAP >> 37) & 0xFF;
	if ((CSS & 1) == 0) { // No NVM command set
		return -ENODEV;
	}

	CC = nvme_read_reg32(ndev, NVMe_CC);
	CC &= ~(0x7 << 4); // CSS = NVM Command Set
	CC &= ~(0xF << 7); // MPS = 4Kb
	CC &= ~(0x7 << 11); // AMS = Round Robin (Supported on all devices)

	CC &= ~(0xF << 16);
	CC |= 0x6 << 16; // IOSQES (IO Submission Queue Entry Size) = 2^6(64)
	CC &= ~(0xF << 20);
	CC |= 0x4 << 20; // IOSQES (IO Completion Queue Entry Size) = 2^4(16)

	nvme_write_reg32(ndev, NVMe_CC, CC);


	if (!nvme_create_admin_queue(ndev, &ndev->admin)) {
		return -ENOMEM;
	}

	u32 AQA =  ((ndev->admin.sq_size-1) & 0xFFF) << 0;
	AQA |= ((ndev->admin.cq_size-1) & 0xFFF) << 16;
	nvme_write_reg32(ndev, NVMe_AQA, AQA);

	nvme_reg32_set(ndev, NVMe_CC, 0); // CC.EN = 1
	while ((nvme_read_reg32(ndev, NVMe_CSTS) & 1) == 0) ;

	if (!nvme_create_io_queue(ndev, &ndev->admin, &ndev->io, 1)) {
		return -ENOMEM;
	}

	if (!nvme_identify_controller(ndev)) {
		return -ENOMEM;
	}

	ndev->ns_count = 0;
	for (u32 i = 0; i < ndev->id_controller.NN; ++i)
	{
		if (!nvme_identify_namespace(ndev, i+1))
			continue;
		if (ndev->id_namespace.NSZE == 0)
			continue;

		u8 fidx = ndev->id_namespace.FLBAS & 0xF;
		u32 block_size = 1 << ndev->id_namespace.lbaf[fidx].LBADS;

		ndev->ns_infos[ndev->ns_count++] = (NVMeNamespaceInfo){
			.nsid = i+1,
			.num_blocks = ndev->id_namespace.NSZE,
			.block_size = block_size,
		};
	}
	if (ndev->ns_count == 0) {
		return -ENODEV;
	}

	err = bdev_create(&ndev->pdev->dev, nvme_block_write, nvme_block_read, ndev->ns_infos[0].num_blocks, ndev->ns_infos[0].block_size, ndev);
	if (err != 0) {
		return err;
	}

	return 0;
}

void nvme_remove(PCIeDevice *pdev)
{
	(void)pdev;
	// devres managed
	// @TODO: shutdown sequence?
}

static PCIeDriver nvme_driver = {
	.match = { .class = 0x1, .subclass = 0x8, .vendor_id = PCI_ANY_ID },
	.probe = nvme_probe,
	.remove = nvme_remove,
	.drv = {
		.name = STR_LIT("nvme_driver"),
	}
};

int nvme_driver_init()
{
	return pci_register_driver(&nvme_driver);
}

void nvme_driver_exit()
{
}

BUILTIN_DRIVER("nvme_driver", nvme_driver_init, nvme_driver_exit)

