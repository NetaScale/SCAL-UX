#include <machine/machdep.h>
#include <vm/vm.h>

#include <stdint.h>

#include "NVMeController.h"
#include "dev/GPTVolumeManager.h"
#include "dev/NVMeDisk.h"
#include "nvmereg.h"

/*
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

/*!
 * Control structure reprezsenting an in-flight NVMe request.
 */
struct nvme_request {
	SIMPLEQ_ENTRY(nvme_request) entry;
	struct dk_diskio_completion *completion;
	uint16_t cid;	/* command ID */
	size_t nbytes;	/* number of bytes to transfer */
	struct nvme_sqe *sqe; /* nulled on submission */
};

struct nvme_queue {
	spinlock_t lock; /* locks all the things in a queue; todo split? */

	SIMPLEQ_HEAD(, nvme_request) req_q;
	struct nvme_request *reqs; /* n = sqslots */

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

static int nvmeId = 0;
static int cmdId = 0; /* todo move to nvme_queue:: */

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

static struct nvme_queue *
queue_alloc(uint16_t idx, uint16_t dstrd)
{
	struct nvme_queue *q = kmem_zalloc(sizeof (*q));
	vm_page_t *page;
	size_t	   stride = 4 << dstrd; /* xxx */

	assert(q != NULL);

	page = vm_pagealloc(1, &vm_pgwiredq);
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

	page = vm_pagealloc(1, &vm_pgwiredq);
	assert(page != NULL);
	q->sq = P2V(page->paddr);
	q->sqslots = PGSIZE / sizeof(struct nvme_sqe);

	q->sqtdbl = NVME_SQTDBL(idx, stride);
	q->cqhdbl = NVME_CQHDBL(idx, stride);

	q->phase = 1;

	SIMPLEQ_INIT(&q->req_q);
	q->reqs = kmem_alloc(sizeof(*q->reqs) * q->sqslots);
	for (int i = 0; i < q->sqslots; i++) {
		q->reqs[i].cid = i;
		q->reqs[i].completion = NULL;
		SIMPLEQ_INSERT_TAIL(&q->req_q, &q->reqs[i], entry);
	}

	return q;
}

static void nvme_intr(md_intr_frame_t *frame, void *arg);

@implementation NVMeController

@synthesize controllerId = m_controllerId;
@synthesize maxBlockTransfer = maxBlockTransfer;

+ (BOOL)probeWithPCIInfo:(dk_device_pci_info_t *)pciInfo
{
	volatile vaddr_t bar0;

	[PCIBus enableMemorySpace:pciInfo];
	[PCIBus enableBusMastering:pciInfo];
	bar0 = P2V([PCIBus getBar:0 info:pciInfo]);

	if (*((uint32_t *)(bar0 + NVME_VS)) == 0xffff) {
		DKLog("NVMe", "Bad BAR mapping");
		return NO;
	}

	return [[self alloc] initWithPCIInfo:pciInfo] != NULL;
}

static void
controller_disable(vaddr_t regs)
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

- (const char *)controllerName
{
	return cident->mn;
}

/*
 * Usable only when there are and will be no async requests in-flight!
 */
- (int)polledSubmit:(struct nvme_sqe *)cmd toQueue:(struct nvme_queue *)queue
{
	int    flags;
	size_t cnt = 0;

	write32(regs + NVME_INTMS, 0x1);

	cmd->cid = cmdId++;

	queue->sq[queue->sqtail++] = *cmd;
	if (queue->sqtail == queue->sqslots)
		queue->sqtail = 0;

	*(uint32_t *)(regs + queue->sqtdbl) = queue->sqtail;

	while (true) {
		flags = queue->cq[queue->cqhead].flags;

		if ((flags & 0x1) == queue->phase)
			break;

		asm("pause");
		if (cnt++ > 0x10000000) {
			fatal("FIXME: add a proper timeout\n");
		}
	}

	assert(queue->cq[queue->cqhead].cid == cmd->cid);

	if (flags >> 1 != 0) {
		DKDevLog(self, "Command error %d\n", flags);
		union nvme_status_code code;
		code.asShort = flags;
		fatal("panicking for debug\n");
	}

	if (++queue->cqhead == queue->cqslots) {
		queue->cqhead = 0;
		queue->phase = !queue->phase;
	}

	*(uint32_t *)(regs + queue->cqhdbl) = queue->cqhead;

	write32(regs + NVME_INTMC, 0x1);

	return flags >> 1;
}

- (int)submitRequest:(struct nvme_request *)req
	   toQueue:(struct nvme_queue *)queue
    withCompletion:(struct dk_diskio_completion *)completion
{
	int iff = md_intr_disable();

	spinlock_lock(&queue->lock);
	req->sqe->cid = req->cid;

	queue->sq[queue->sqtail++] = *req->sqe;
	req->sqe = NULL;
	if (queue->sqtail == queue->sqslots)
		queue->sqtail = 0;

	*(uint32_t *)(regs + queue->sqtdbl) = queue->sqtail;

	spinlock_unlock(&queue->lock);
	md_intr_x(iff);

	return 0;
}

- (void)queueCompleteRequests:(struct nvme_queue *)queue
{
	spinlock_lock(&queue->lock);
	while (true) {
		uint16_t flags = queue->cq[queue->cqhead].flags;
		uint16_t cid = queue->cq[queue->cqhead].cid;
		struct nvme_request *req;

		if ((flags & 0x1) != queue->phase)
			break;

		if (flags >> 1 != 0) {
			DKDevLog(self, "Command error %d\n", flags);
			union nvme_status_code code;
			code.asShort = flags;
			fatal("panicking for debugging purposes\n");
		}

		assert(cid < queue->cqslots);
		req = &queue->reqs[cid];
		assert (req->completion);
		req->completion->callback(req->completion->data, req->nbytes);

		/*
		 * todo(med): free PRP list? i think better we just keep PRP
		 * list allocated and associated with an nvme_req and reuse it.
		 */

		/* return request to free queue */
		req->completion = NULL;
		req->nbytes = 0;
		SIMPLEQ_INSERT_HEAD(&queue->req_q, req, entry);

		if (++queue->cqhead == queue->cqslots) {
			queue->cqhead = 0;
			queue->phase = !queue->phase;
		}

		*(uint32_t *)(regs + queue->cqhdbl) = queue->cqhead;
	}
	spinlock_unlock(&queue->lock);
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
	vm_page_t	  *page = vm_pagealloc(1, &vm_pgwiredq);
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
	/* TODO(low): handle non 512 byte block size */
	maxBlockTransfer = (1 << cident->mdts) * PGSIZE / 512;

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
	assert(status == 0);

	/* completion queue */
	create.opcode = NVM_ADMIN_ADD_IOSQ;
	create.cqid = qid;
	create.prp1 = (uint64_t)V2P(queue->sq);
	create.qsize = queue->sqslots - 1;
	create.cqid = qid;	      /* completion queue */
	create.qflags = NVM_SQE_Q_PC; /* physically contiguous */

	status = [self polledSubmit:(struct nvme_sqe *)&create toQueue:adminq];
	assert(status == 0);

	return queue;
}

/* for GDB debugging purposes */
#undef malloc
void *malloc(size_t size)
{
	return kmem_alloc(size);
}


- initWithPCIInfo:(dk_device_pci_info_t *)pciInfo
{
	struct nvme_cap cap;
	struct nvme_ver ver;
	vm_page_t *page = vm_pagealloc(1, &vm_pgwiredq);
	int r;

	self = [super initWithProvider:pciInfo->busObj];
	m_controllerId = nvmeId++;
	kmem_asprintf(&m_name, "NVMe%d", m_controllerId);

	[self registerDevice];
	DKLogAttach(self);

	r = [PCIBus handleInterruptOf:pciInfo
			  withHandler:nvme_intr
			     argument:self
			   atPriority:kSPL0];

	if (r < 0) {
		DKDevLog(self, "Failed to allocate interrupt handler: %d\n", r);
		[self release];
		return NULL;
	}

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

	controller_disable(regs);
	if ([self enable] < 0) {
		DKDevLog(self, "Failed to re-enable controller!\n");
		[self release];
		return nil;
	}

	[PCIBus setInterruptsOf:pciInfo enabled:YES];

	[self identifyController];

	/* create I/O submission and completion queues */
	/* just one pair for now; eventually one per CPU would be nice */
	ioqueue = [self createQueuePairWithID:1];
	assert(ioqueue != NULL);

	/* identify namespaces */
	for (int i = 1; i < cident->nn; i++) {
		struct nvm_identify_namespace *nsident = P2V(page->paddr);
		struct nvme_disk_attach	       diskAttachInfo;

		nsident->nsze = 0;
		[self identifyNamespace:i out:nsident];
		if (nsident->nsze == 0)
			continue;

		diskAttachInfo.controller = self;
		diskAttachInfo.nsid = i;
		diskAttachInfo.nsident = nsident;

		NVMeDisk *disk = [[NVMeDisk alloc]
		    initWithAttachmentInfo:&diskAttachInfo];

		(void)disk; /* TODO: keep track of it? */
	}

	return self;
}

- (int)readBlocks:(blksize_t)nBlocks
	       at:(blkoff_t)offset
	     nsid:(uint16_t)nsid
       intoBuffer:(vm_mdl_t *)buf
       completion:(struct dk_diskio_completion *)completion
{
	int	  r;
	int 	  iff = md_intr_disable();
	struct nvme_sqe_io io = { 0 };
	struct nvme_request *req;
	vm_mdl_t *prpListMDL;

	spinlock_lock(&ioqueue->lock);
	req = SIMPLEQ_LAST(&ioqueue->req_q, nvme_request, entry);
	assert(req != NULL);
	SIMPLEQ_REMOVE(&ioqueue->req_q, req, nvme_request, entry);
	spinlock_unlock(&ioqueue->lock);
	md_intr_x(iff);

	req->completion = completion;
	req->sqe = (struct nvme_sqe *)&io;
	/* TODO(low): block size other than 512 bytes */
	req->nbytes = nBlocks * 512;
	io.opcode = NVM_CMD_READ;
	io.nsid = nsid;
	io.slba = offset;
	io.nlb = nBlocks - 1;

	assert(nBlocks < maxBlockTransfer);

	if (buf->nPages == 1) {
		io.entry.prp[0] = (uint64_t)buf->pages[0]->paddr;
	} else if (buf->nPages == 2) {
		io.entry.prp[0] = (uint64_t)buf->pages[0]->paddr;
		io.entry.prp[1] = (uint64_t)buf->pages[1]->paddr;
	} else {
		size_t nPRPPerList = PGSIZE / sizeof(void *) - 1;
		size_t nPRPLists = ROUNDUP((buf->nPages - 1), nPRPPerList) /
			nPRPPerList + 1;
		size_t iPRPList = 0;

		r = vm_mdl_new_with_capacity(&prpListMDL, nPRPLists * PGSIZE);
		assert(r >= 0);

		for (int i = 1; i < buf->nPages; i++) {
			void **entry = prpListMDL->pages[iPRPList]->paddr +
			    (i - 1) * sizeof(void *);

			entry = P2V(entry);

			if (i > 1 && (i - 1) % nPRPPerList == 0) {
				/* place the pointer to next PRP list */
				*entry = prpListMDL->pages[++iPRPList]->paddr;
				i--;
				continue;
			}

			*entry = buf->pages[i]->paddr;
		}

		io.entry.prp[0] = (uint64_t)buf->pages[0]->paddr;
		io.entry.prp[1] = (uint64_t)prpListMDL->pages[0]->paddr;
	}

	r = [self submitRequest:req toQueue:ioqueue withCompletion:completion];

	return r;
}

@end

static void
nvme_intr(md_intr_frame_t *frame, void *arg)
{
	NVMeController *controller = arg;

	[controller queueCompleteRequests:controller->ioqueue];
}
