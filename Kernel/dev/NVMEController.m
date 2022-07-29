#include <stdint.h>

#include "NVMEController.h"
#include "kern/vm.h"
#include "nvmereg.h"

/**
 * Definitions from the NVM Express Base Specification, Revision 1.4c
 */

struct __attribute__((packed)) nvme_cap {
	uint64_t MQES : 16, /* maximum queue entries supported */
	    CQR : 1,	    /* continuous queues required */
	    AMS : 2,	    /* arbitrary mechanism supported */
	    reserved : 5,   /* ... */
	    TO : 8,	    /* timeout, in units 500ms */
	    DSTRD : 4,	    /* doorbell stride */
	    NSSRS : 1,	    /* NVM subsystem reset supported */
	    CSS : 8,	    /* command set supported */
	    BPS : 1,	    /* boot partition supported */
	    reserved1 : 2,  /* ... */
	    MPSMIN : 4,	    /* memory page size min */
	    MPSMAX : 4,	    /* memory page size max */
	    PMRS : 1,	    /* persistent memory region supported */
	    CMBS : 1,	    /* controller memory buffer supported */
	    reserved2 : 6;
};

struct __attribute__((packed)) nvme_aqa {
	uint32_t ASQS : 12, /* count entries admin submit queue, minus 1 */
	    reserved : 4,   /* ... */
	    ACQS : 12,	    /* count entries admin completion queue, minus 1 */
	    reserved1 : 4;  /* ... */
};

struct __attribute__((packed)) nvme_cc {
	uint32_t EN : 1,   /* enable */
	    reserved : 3,  /* ... */
	    CSS : 3,	   /* command set supported */
	    MPS : 4,	   /* machine page size */
	    AMS : 3,	   /* arbitration mechanism supported */
	    SHN : 2,	   /* */
	    IOSQES : 4,	   /* */
	    IOCQES : 4,	   /* */
	    reserved1 : 8; /* ... */
};

struct __attribute__((packed)) nvme_csts {
	uint32_t RDY : 1, /* ready */
	    CFS : 1,	  /* controller fatal status */
	    SHST : 2,	  /* shutdown status */
	    NSSRO : 1,	  /* subsystem reset occurred */
	    PP : 1,	  /* processing paused */
	    reserved : 26;
};

struct nvme_ver {
	uint8_t	 ter;
	uint8_t	 min;
	uint16_t maj;
};

union nvme_status_code {
	struct {
		uint16_t P : 1,	  /* phase */
		    SC : 8,	  /* status code */
		    SCT : 3,	  /* status code type */
		    reserved : 2, /* ... */
		    M : 1,	  /* more */
		    DNR : 1;	  /* do not retry */
	};
	uint16_t asShort;
};

_Static_assert(sizeof(struct nvme_cap) == sizeof(uint64_t), "NVMe caps size");
_Static_assert(sizeof(struct nvme_cc) == sizeof(uint32_t), "NVMe cc size");

struct nvme_queue {
	voff_t sqtdbl; /* offset submission queue tail doorbell */
	voff_t cqhdbl; /* offset completion queue head doorbell */
	volatile struct nvme_sqe *sq; /* vaddr submission queue */
	volatile struct nvme_cqe *cq; /* vaddr completion queue */

	size_t	 sqslots; /* number of slots in sq */
	uint16_t sqtail;
	size_t	 cqslots; /* number of slots in cq */
	uint16_t cqhead;

	uint8_t phase;
};

static int controller_id = 0;

static void
copy32(void *dst, void *src, size_t nbytes)
{
	assert(nbytes % 4 == 0);

	for (int i = 0; i < nbytes; i += 4) {
		*(uint32_t *)(dst + i) = *(uint32_t *)(src + i);
	}
}

static void
write32(void *dst, uint32_t val)
{
	*(uint32_t *)dst = val;
}

struct nvme_queue *
queue_alloc(uint16_t idx, uint16_t dstrd)
{
	struct nvme_queue *q = kcalloc(1, sizeof *q);
	vm_page_t	  *page;
	size_t		   stride = 4 << dstrd; /* xxx */

	assert(q != NULL);

	page = vm_pagealloc_zero(1);
	assert(page != NULL);
	q->cq = P2V(page->paddr);
#if 0
	q->cqslots = PGSIZE / sizeof(struct nvme_cqe);
#else
	/*
	 * this is necessary for VirtualBox for some reason, differing sizes at
	 * least for the admin queue don't work with it.
	 */
	q->cqslots = PGSIZE / sizeof(struct nvme_sqe);
#endif

	page = vm_pagealloc_zero(1);
	assert(page != NULL);
	q->sq = P2V(page->paddr);
	q->sqslots = PGSIZE / sizeof(struct nvme_sqe);

	q->sqtdbl = NVME_SQTDBL(idx, stride);
	q->cqhdbl = NVME_CQHDBL(idx, stride);

	q->phase = 1;

	return q;
}

static void nvme_intr(intr_frame_t *frame, void *arg);

@implementation NVMEController

+ (BOOL)probeWithPCIInfo:(dk_device_pci_info_t *)pciInfo
{
	volatile vaddr_t bar0;
	int		 r;

	[PCIBus enableMemorySpace:pciInfo];
	[PCIBus enableBusMastering:pciInfo];
	bar0 = P2V([PCIBus getBar:0 info:pciInfo]);

	if (*((uint32_t *)(bar0 + NVME_VS)) == 0xffff) {
		DKLog("NVMe", "Bad BAR mapping");
		return NO;
	}

	[PCIBus setInterruptsOf:pciInfo enabled:NO];
	r = [PCIBus handleInterruptOf:pciInfo
			  withHandler:nvme_intr
			     argument:self
			   atPriority:kSPL0];

	if (r < 0) {
		DKLog("NVMe", "Failed to allocate interrupt handler: %d\n", r);
		return NO;
	}

	return [[self alloc] initWithPCIInfo:pciInfo] != NULL;
}

static void
disable(vaddr_t regs)
{
	struct nvme_cc	 cc;
	struct nvme_csts csts;

	copy32(&cc, regs + NVME_CC, sizeof cc);
	cc.EN = 0;
	copy32(regs + NVME_CC, &cc, sizeof cc);

	while (true) {
		copy32(&csts, regs + NVME_CSTS, sizeof csts);
		if (csts.RDY == 0)
			break;
		asm("pause");
	}
}

size_t subid = 0;

- (int)polledSubmit:(struct nvme_sqe *)cmd toQueue:(struct nvme_queue *)queue
{
	int    flags;
	size_t cnt = 0;

	cmd->cid = subid++;
	__sync_synchronize();
	adminq->sq[adminq->sqtail++] = *cmd;
	if (adminq->sqtail == adminq->sqslots)
		adminq->sqtail = 0;
	__sync_synchronize();
	*(uint32_t *)(regs + adminq->sqtdbl) = adminq->sqtail;

	while (true) {
		flags = adminq->cq[adminq->cqhead].flags;

		if ((flags & 0x1) == adminq->phase)
			break;

		asm("pause");
		if (cnt++ > 0x10000000) {
			kprintf("FIXME: add a proper timeout\n");
			for (;;)
				asm("hlt");
		}
	}

	assert(adminq->cq[adminq->cqhead].cid == cmd->cid);

	if (flags >> 1 != 0) {
		DKDevLog(self, "Command error %d\n", flags);
		union nvme_status_code code;
		code.asShort = flags;
		for (;;)
			;
	}

	if (++adminq->cqhead == adminq->cqslots) {
		adminq->cqhead = 0;
		adminq->phase = !adminq->phase;
	}

	__sync_synchronize();
	*(uint32_t *)(regs + adminq->cqhdbl) = adminq->cqhead;

	return flags >> 1;
}

#define TRIMSPACES(CHARARR)                                                   \
	for (char *mn = CHARARR + sizeof(CHARARR) - 1; mn >= CHARARR; mn--) { \
		if (*mn == ' ')                                               \
			*mn = '\0';                                           \
		else                                                          \
			break;                                                \
	}

- (void)identifyController
{
	vm_page_t	  *page = vm_pagealloc_zero(1);
	struct nvme_sqe cmd = { 0 };

	cident = P2V(page->paddr);

	cmd.opcode = 0x06; /* admin identify */
	cmd.nsid = 0;
	cmd.cdw10 = 1; /* get controller info */
	cmd.entry.prp[0] = (uint64_t)V2P(cident);

	[self polledSubmit:&cmd toQueue:adminq];

	TRIMSPACES(cident->mn);
	TRIMSPACES(cident->fr);
	TRIMSPACES(cident->sn);

	DKDevLog(self, "%s, firmware %s, serial %s\n", cident->mn, cident->fr,
	    cident->sn);
}

/* out must be a pointer in the HHDM to a page */
- (int)identifyNamespace:(uint8_t)nsNum out:(struct nvm_identify_namespace *)out
{
	struct nvme_sqe cmd = { 0 };

	cmd.opcode = 0x06; /* admin identify */
	cmd.nsid = nsNum;
	cmd.cdw10 = 0; /* get namespace info */
	cmd.entry.prp[0] = (uint64_t)V2P(out);

	return [self polledSubmit:&cmd toQueue:adminq];
}

- (int)enable
{
	struct nvme_aqa	 aqa = { 0 };
	struct nvme_cc	 cc = { 0 };
	struct nvme_csts csts;

	aqa.ASQS = adminq->sqslots - 1;
	aqa.ACQS = adminq->cqslots - 1;
	copy32(regs + NVME_AQA, &aqa, sizeof(aqa));

	cc.AMS = 0;
	cc.EN = 1;
	cc.IOSQES = 6; /* 64 bytes */
	cc.IOCQES = 4; /* 16 bytes */
	cc.CSS = 0;    /* NVMe command set */
	cc.MPS = 0;    /* 4KiB pages */

	*(paddr_t *)(regs + NVME_ACQ) = V2P(adminq->cq);
	*(paddr_t *)(regs + NVME_ASQ) = V2P(adminq->sq);
	copy32(regs + NVME_CC, &cc, sizeof cc);

	while (true) {
		copy32(&csts, regs + NVME_CSTS, sizeof csts);
		if (csts.RDY == 1)
			break;
		else if (csts.CFS) {
			DKDevLog(self, "controller fatal status\n");
			return -1;
		}
	}

	return 0;
}

- (struct nvme_queue *)createQueuePairWithID:(uint16_t)qid
{
	struct nvme_queue *queue = queue_alloc(qid, dstrd);
	uint16_t	   status;
	struct nvme_sqe_q  create = { 0 };

	assert(queue != NULL);

	/* completion queue */
	create.opcode = NVM_ADMIN_ADD_IOCQ;
	create.qid = qid;
	create.prp1 = (uint64_t)V2P(queue->cq);
	create.qsize = queue->cqslots - 1;
	create.cqid = 0; /* IRQ vector */
	/* physically contiguous, interrupts enabled */
	create.qflags = NVM_SQE_Q_PC | NVM_SQE_CQ_IEN;

	status = [self polledSubmit:(struct nvme_sqe *)&create toQueue:adminq];
	assert (status == 0);

	/* completion queue */
	create.opcode = NVM_ADMIN_ADD_IOSQ;
	create.cqid = qid;
	create.prp1 = (uint64_t)V2P(queue->sq);
	create.qsize = queue->sqslots - 1;
	create.cqid = qid;	     /* completion queue */
	create.qflags = NVM_SQE_Q_PC; /* physically contiguous */

	status = [self polledSubmit:(struct nvme_sqe *)&create toQueue:adminq];
	assert(status == 0);

	return queue;
}

- initWithPCIInfo:(dk_device_pci_info_t *)pciInfo
{
	struct nvme_cap cap;
	struct nvme_ver ver;
	vm_page_t	  *page = vm_pagealloc_zero(1);

	self = [super init];
	ksnprintf(name, sizeof name, "NVMeController%d", controller_id++);

	[self registerDevicePCIInfo:pciInfo];

	regs = P2V([PCIBus getBar:0 info:pciInfo]);
	copy32(&cap, regs + NVME_CAP, sizeof cap);
	copy32(&ver, regs + NVME_VS, sizeof ver);

	if (ver.ter)
		DKDevLog(self, "NVMe version %d.%d.%d\n", ver.maj, ver.min,
		    ver.ter);
	else
		DKDevLog(self, "NVMe version %d.%d\n", ver.maj, ver.min);

	adminq = queue_alloc(0, cap.DSTRD);

	assert(cap.MPSMIN == 0 && "doesn't support host pagesize");

	disable(regs);
	if ([self enable] < 0) {
		[self release];
		return nil;
	}

	write32(regs + NVME_INTMS, 0x1);
	[self identifyController];

	/* identify namespaces */
	for (int i = 1; i < cident->nn; i++) {
		struct nvm_identify_namespace *nsident = P2V(page->paddr);
		size_t			       secs;

		nsident->nsze = 0;
		[self identifyNamespace:i out:nsident];
		if (nsident->nsze == 0)
			continue;

		secs =
		    1 << nsident->lbaf[NVME_ID_NS_FLBAS(nsident->flbas)].lbads;

		kprintf(
		    "namespace %d: %lu sectors, %lu bytes/sector, %luKiB \n", i,
		    nsident->nsze, secs, secs * nsident->nsze / 1024);
	}

	/* create I/O submission and completion queues */
	/* just one pair for now; eventually one per CPU would be nice */
	ioqueue = [self createQueuePairWithID:1];
	assert(ioqueue != NULL);

	write32(regs + NVME_INTMC, 0x1);
	[PCIBus setInterruptsOf:pciInfo enabled:YES];

	DKDevLog(self, "Done\n");
	return self;
}

@end

static void
nvme_intr(intr_frame_t *frame, void *arg)
{
	kprintf("NVMe interrupt\n");

	md_eoi();
}
