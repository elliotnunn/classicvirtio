/* Copyright (c) 2024 Elliot Nunn */
/* Licensed under the MIT license */

/*
Driver for virtio-blk under the Macintosh Device Manager

Works with unpartitioned disks (more convenient than the Mac SCSI Manager).
Chooses an arbitrary HFS partition from a partitioned disk.

Synchronous, because there were mysterious deadlocks with an async driver.
The slowdown is probably negligible on a host with an SSD.

Does not yet seem to work right on PowerPC hosts!

Read-only for now.
*/

#include <Devices.h>
#include <Disks.h>
#include <DriverGestalt.h>
#include <DriverServices.h>
#include <Events.h>
#include <Files.h>
#include <HFSVolumes.h>
#include <Memory.h>
#include <Traps.h>
#include <Types.h>
#include <stdbool.h>
#include <string.h>

#include "allocator.h"
#include "cleanup.h"
#include "log.h"
#include "panic.h"
#include "paramblkprint.h"
#include "printf.h"
#include "transport.h"
#include "virtqueue.h"

#include "device.h"

struct config {
	uint64_t capacity;
	uint32_t size_max;
	uint32_t seg_max;
	struct virtio_blk_geometry {
		uint16_t cylinders;
		uint8_t heads;
		uint8_t sectors;
	} geometry;
	uint32_t blk_size;
	struct virtio_blk_topology {
		uint8_t physical_block_exp; // log2(n(logical blocks per physical block))
		uint8_t alignment_offset; // offset of first aligned logical block
		uint16_t min_io_size; // suggested minimum I/O size in blocks
		uint32_t opt_io_size; // optimal (suggested maximum) I/O size in blocks
	} topology;
	uint8_t writeback;
	uint8_t unused0;
	uint16_t num_queues;
	uint32_t max_discard_sectors;
	uint32_t max_discard_seg;
	uint32_t discard_sector_alignment;
	uint32_t max_write_zeroes_sectors;
	uint32_t max_write_zeroes_seg;
	uint8_t write_zeroes_may_unmap;
	uint8_t unused1[3];
	uint32_t max_secure_erase_sectors;
	uint32_t max_secure_erase_seg;
	uint32_t secure_erase_sector_alignment;
} __attribute((scalar_storage_order("little-endian")));

struct request {
		uint32_t type;
		uint32_t reserved;
		uint64_t sector;
} __attribute((scalar_storage_order("little-endian")));

struct fixedbuf {
	struct request request;
	char pad[2048-sizeof (struct request)];
	volatile char reply;
	char pad2[1024-1];
	char sector[512];
} ;

static void installDrive(void);
static void removeDrive(void);
static struct DrvQEl *findDrive(short num);
static void *readSector(int which);
static OSErr stressfulGetPhysical(LogicalToPhysicalTable *addresses, unsigned long *physicalEntryCount);
void probePartitions(uint32_t *firstblock, uint32_t *numblocks);
static OSErr cIcon(struct CntrlParam *pb);
static OSErr cDriveInfo(struct CntrlParam *pb);
static OSErr dgNameRegistryEntry(struct DriverGestaltParam *pb);
static OSErr dgOpenFirmwareBoot(struct DriverGestaltParam *pb);
static OSErr dgBoot(struct DriverGestaltParam *pb);
static OSErr dgDeviceReference(struct DriverGestaltParam *pb);
static OSErr dgInterface(struct DriverGestaltParam *pb);
static OSErr dgDeviceType(struct DriverGestaltParam *pb);
static OSErr controlStatusCall(struct CntrlParam *pb);
static OSErr controlStatusDispatch(long selector, void *pb);

enum {
	MAXBUFFERS = 16,
};

static struct fixedbuf *fixedbuf;
static uint32_t pfixedbuf;
static int buffers; // more buffers means more fragmentation tolerated
static short drvrRefNum;
uint32_t firstblock, numblocks;
static struct DrvSts2 dqe = {
	.track = 0,       // not for us
	.writeProt = 0,   // set once Virtio device is probed
	.diskInPlace = 8, // nonejectable
	.installed = 1,   // refers to the drive, not the disk
	.sides = 0x80,    // double-sided, why not
	.qLink = NULL,
	.qType = 0,
	.dQDrive = 0,     // set by AddDrive
	.dQRefNum = 0,    // set by AddDrive
	.dQFSID = 0,      // HFS
	.driveSize = 0,   // (LS16 of sector cnt) set once Virtio device is probed
	.driveS1 = 0,     // (MS16 of sector cnt)
	.driveType = 0,
	.driveManf = 0,
	.driveChar = 0,
	.driveMisc = 0,
};

DriverDescription TheDriverDescription = {
	kTheDescriptionSignature,
	kInitialDriverDescriptor,
	{"\x0cpci1af4,1001", {0x00, 0x10, 0x80, 0x00}}, // v0.1
	{kDriverIsLoadedUponDiscovery |
		kDriverIsOpenedUponLoad,
		"\x0c.VirtioBlock"},
	{1, // nServices
	{{kServiceCategoryNdrvDriver, kNdrvTypeIsGeneric, {0x00, 0x10, 0x80, 0x00}}}} //v0.1
};

// Remember that this only needs to allow/deny the request, cleanup.c handles the rest
int DriverStop(void) {
	// Search the volume queue for a mounted+online volume pointing to us
	for (VCB *vcb=GetVCBQHdr()->qHead; vcb!=NULL&&vcb!=(VCB *)-1; vcb=(VCB *)vcb->qLink) {
		if (vcb->vcbDrvNum == dqe.dQDrive) {
			printf("Refusing to stop while volume is mounted\n");
			return closErr;
		}
	}

	printf("Stopping\n");
	return noErr;
}

int DriverStart(short refNum) {
	drvrRefNum = refNum;
	InitLog();
	sprintf(LogPrefix, "Block(%d) ", refNum);

	if (!VInit(refNum)) {
		printf("Transport layer failure\n");
		goto openErr;
	};

	dqe.writeProt = 0x80 * VGetDevFeature(5); // VIRTIO_BLK_F_RO
	VSetFeature(5, VGetDevFeature(5)); // SHOULD use feature if offered
	if (!VFeaturesOK()) {
		printf("Feature negotiation failure\n");
		goto openErr;
	}

	fixedbuf = AllocPages(1, &pfixedbuf);
	if (fixedbuf == NULL) {
		printf("Memory allocation failure\n");
		goto openErr;
	}
	RegisterCleanupVoidPtr(FreePages, fixedbuf);

	VDriverOK();

	buffers = QInit(0, MAXBUFFERS);
	if (buffers < 4) {
		printf("Virtqueue layer failure\n");
		goto openErr;
	}

	// Probe the disk
	struct config *config = VConfig;
	printf("Device size: %#llx bytes, %#llx blocks\n", config->capacity*512, config->capacity);
	firstblock = 0;
	numblocks = config->capacity; // 2^32 block max for Mac OS
	probePartitions(&firstblock, &numblocks);
	printf("Volume size: %#llx bytes, %#lx blocks, %#lx skipblocks\n", (uint64_t)numblocks*512, numblocks, firstblock);
	struct HFSMasterDirectoryBlock *mdb = readSector(2); // now adjusted for partition
	if (mdb->drSigWord == 0x4244) {
		printf("HFS volume name: %.*s\n", *mdb->drVN, mdb->drVN+1);
	}
	dqe.driveSize = numblocks;
	dqe.driveS1 = numblocks >> 16;

	installDrive();
	RegisterCleanup(removeDrive);

	if (*(char *)0x14a >= 0) { // check whether Event Mgr is actually up
		PostEvent(diskEvt, dqe.dQDrive);
	}

	printf("Ready\n");
	return noErr;
openErr:
	VFail();
	return openErr;
}

int DriverRead(IOParam *pb) {
	if (LogEnable) printf("%s", PBPrint(pb, pb->ioTrap|0xa000, 1));
	int err = noErr;

	// First element is a logical buffer, subsequent ones are the result of GetPhysical
	MemoryBlock memblocks[1+MAXBUFFERS] = {pb->ioBuffer, pb->ioReqCount};
	LockMemory(pb->ioBuffer, pb->ioReqCount); // already held by VM
	pb->ioActCount = 0;
	fixedbuf->request.type = 0;

	// Might take a few goes if there is mem fragmentation
	while (pb->ioActCount != pb->ioReqCount) {
		fixedbuf->request.sector = firstblock + (pb->ioPosOffset+pb->ioActCount)/512;

		unsigned long n = buffers - 2; // two buffers reserved for request (16b) and status (1b)
		GetPhysical((LogicalToPhysicalTable *)memblocks, &n); // how many physical extents were actually needed?
		uint32_t phys[MAXBUFFERS] = {pfixedbuf + offsetof(struct fixedbuf, request)};
		uint32_t size[MAXBUFFERS] = {sizeof fixedbuf->request};
		for (int i=1; i<=n; i++) {
			phys[i] = (uint32_t)memblocks[i].address;
			size[i] = (uint32_t)memblocks[i].count;
		}
		phys[1+n] = pfixedbuf + offsetof(struct fixedbuf, reply);
	 	size[1+n] = sizeof(fixedbuf->reply);

		QSend(0, 1/*n_out*/, n+1/*n_in*/, phys, size, NULL, true/*wait*/);

		if (fixedbuf->reply != 0) {
			panic("bad reply");
		}

		for (int i=1; i<=n; i++) {
			pb->ioActCount += memblocks[i].count;
		}
	}
	UnlockMemory(pb->ioBuffer, pb->ioReqCount);

	if (LogEnable) printf("%s", PBPrint(pb, pb->ioTrap|0xa000, err));
	return err;
}

int DriverWrite(IOParam *pb) {
	return writErr;
}

static void installDrive(void) {
	short driveNum = 8; // conventional lowest number for HD
	while (findDrive(driveNum) != NULL) driveNum++;
	AddDrive(drvrRefNum, driveNum, (DrvQEl *)&dqe.qLink);
	printf("Drive number: %d\n", dqe.dQDrive);
}

static void removeDrive(void) {
	int err = Dequeue((DrvQEl *)&dqe.qLink, GetDrvQHdr());
	printf("the dequeue call returned %d\n", err);
}

// static void removeDrive(void *drive) {
// 	Dequeue(drive, GetDrvQHdr());
// }

static struct DrvQEl *findDrive(short num) {
	for (struct DrvQEl *i=(struct DrvQEl *)GetDrvQHdr()->qHead;
		i!=NULL;
		i=(struct DrvQEl *)i->qLink
	) {
		if (i->dQDrive == num) return i;
	}
	return NULL;
}

// Utility routine for probing the disk at startup
static void *readSector(int which) {
	struct IOParam pb;
	pb.ioTrap = _Read;
	pb.ioPosOffset = 512L * which;
	pb.ioReqCount = 512;
	pb.ioBuffer = fixedbuf->sector;
	DriverRead(&pb);
	return fixedbuf->sector;
}

// Use instead of GetPhysical
// Randomly cuts the first block into two extents, and defer the rest till later
static OSErr stressfulGetPhysical(LogicalToPhysicalTable *addresses, unsigned long *physicalEntryCount) {
	MemoryBlock *theirtable = (MemoryBlock *)addresses;
	MemoryBlock mytable[2] = {theirtable[0]/*log*/};
	unsigned long mycount = 1; // only one extent please
	GetPhysical((LogicalToPhysicalTable *)mytable, &mycount);
	if (mycount == 0) {
		panic("no extents");
	}

	static uint32_t rand = 12345;
	int cut = 256;
	do { // Marsaglia's xorshift 32
		rand ^= rand << 13;
		rand ^= rand >> 17;
		rand ^= rand << 5;
		cut = rand % 512;
	} while (cut == 0);

	theirtable[0/*log*/].address += 512;
	theirtable[0/*log*/].count -= 512;
	theirtable[1/*phy*/].address = mytable[1].address;
	theirtable[1/*phy*/].count = cut;
	theirtable[2/*phy*/].address = mytable[1].address + cut;
	theirtable[2/*phy*/].count = 512 - cut;
	*physicalEntryCount = 2;
	return 0;
}

// If the disk is partitioned then select a single HFS partition
void probePartitions(uint32_t *firstblock, uint32_t *numblocks) {
	struct Block0 blk0;
	memcpy(&blk0, readSector(0), sizeof blk0);
	if (blk0.sbSig != 0x4552) {
		return;
	}

	struct Partition part;
	int index = 0;
	struct Partition chosen;
	do {
		memcpy(&part, readSector(blk0.sbBlkSize / 512 * ++index), sizeof part);
		printf("Partition #%d type=%-24s name=%s", index, part.pmParType, part.pmPartName);
		if (!strcmp(part.pmParType, "Apple_HFS")) {
			printf("  *selected*");
			chosen = part;
		}
		printf("\n");
	} while (index < part.pmMapBlkCnt);

	if (chosen.pmSig) {
		*firstblock = blk0.sbBlkSize / 512 * chosen.pmPyPartStart;
		*numblocks = blk0.sbBlkSize / 512 * chosen.pmPartBlkCnt;
	}
}

void DNotified(uint16_t q, volatile uint32_t *retlen) {
}

void DConfigChange(void) {
}

static OSErr cIcon(struct CntrlParam *pb) {
	struct about {
		uint32_t icon[64];
		unsigned char location[64];
	};

	// B&W HD icon, Sys 8+ converts to colour version
	static struct about hd = {
		0x00000000, 0x00000000, 0x00000000, 0x00000000, // Icon
		0x00000000, 0x00000000, 0x00000000, 0x00000000,
		0x00000000, 0x00000000, 0x00000000, 0x00000000,
		0x00000000, 0x00000000, 0x00000000, 0x00000000,
		0x00000000, 0x00000000, 0x7ffffffe, 0x80000001,
		0x80000001, 0x80000001, 0x80000001, 0x80000001,
		0x80000001, 0x88000001, 0x80000001, 0x80000001,
		0x7ffffffe, 0x00000000, 0x00000000, 0x00000000,
		0x00000000, 0x00000000, 0x00000000, 0x00000000, // Mask
		0x00000000, 0x00000000, 0x00000000, 0x00000000,
		0x00000000, 0x00000000, 0x00000000, 0x00000000,
		0x00000000, 0x00000000, 0x00000000, 0x00000000,
		0x00000000, 0x00000000, 0x7ffffffe, 0xffffffff,
		0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff,
		0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff,
		0x7ffffffe, 0x00000000, 0x00000000, 0x00000000,
		"\x13" "Virtio block device"
	};

	static const void *ret = &hd;
	memcpy(pb->csParam, &ret, sizeof ret);
	return noErr;
}

static OSErr cDriveInfo(struct CntrlParam *pb) {
	uint32_t ret =
		(0 << 8) |  // set for external, clear for internal
		(1 << 9) |  // set if SCSI, clear if IWM
		(1 << 10) | // set if fixed, clear if can be removed
		(1 << 11) | // set for secondary drives, clear for primary drive
		1;

	memcpy(pb->csParam, &ret, sizeof ret);
	return noErr;
}

static OSErr sDriveStatus(struct CntrlParam *pb) {
	memcpy(pb->csParam, &dqe, 22);
	return noErr;
}

// When the /chosen "bootpath" property is set to this PCI device,
// the following three Driver Gestalt handlers tell the NewWorld ROM
// to root from this volume -- without waiting for the ?-floppy to time out.

// Essential to boot from 9P
static OSErr dgNameRegistryEntry(struct DriverGestaltParam *pb) {
#if GENERATINGCFM // PowerPC NDRV
	static RegEntryID dev;
	GetDriverInformation(drvrRefNum,
		(UnitNumber []){0},             // junk
		(DriverFlags []){0},            // junk
		(DriverOpenCount []){0},        // junk
		(Str255){},                     // junk
		&dev,                           // return value of interest
		&(CFragSystem7Locator){.u={.onDisk={.fileSpec=&(FSSpec){}}}}, // junk that needs valid ptr
		(CFragConnectionID []){0},      // junk
		(DriverEntryPointPtr []){NULL}, // junk
		(DriverDescription []){{}});    // junk
	pb->driverGestaltResponse = (long)&dev;
	return noErr;
#else // 68k DRVR
	return statusErr;
#endif
}

// Essential to boot from 9P
// kOFBootNotPartitioned/kOFBootAnyPartition take the same code path in StartLib
// kOFBootSpecifiedPartition takes a separate path but still works
// kOFBootNotBootable doesn't work (naturally)
static OSErr dgOpenFirmwareBoot(struct DriverGestaltParam *pb) {
	pb->driverGestaltResponse = kOFBootNotPartitioned;
	return noErr;
}

// Essential to boot from 9P
// Follows a four-byte structure ?inherited from a Slot Manager PRAM field
// The upper 5 bits must equal a fake SCSI ID, which is (unit number - 32)
static OSErr dgBoot(struct DriverGestaltParam *pb) {
	long unitNum = ~drvrRefNum;
	long scsiNum = unitNum - 32;
	pb->driverGestaltResponse = scsiNum << 27;
	return noErr;
}

// No effect on boot, opaque to the system
static OSErr dgDeviceReference(struct DriverGestaltParam *pb) {
	pb->driverGestaltResponse = 0;
	return noErr;
}

// No effect on boot, even when answer is quite silly
static OSErr dgInterface(struct DriverGestaltParam *pb) {
	pb->driverGestaltResponse = kdgExtBus;
	return noErr;
}

// No effect on boot
static OSErr dgDeviceType(struct DriverGestaltParam *pb) {
	pb->driverGestaltResponse = kdgDiskType;
	return noErr;
}

int DriverCtl(CntrlParam *pb) {
	if (LogEnable) printf("%s", PBPrint(pb, pb->ioTrap|0xa000, 1));

	int err = controlErr;
	if (pb->csCode == kDriveIcon) {
		err = cIcon(pb);
	} else if (pb->csCode == kMediaIcon) {
		err = cIcon(pb);
	} else if (pb->csCode == kDriveInfo) {
		err = cDriveInfo(pb);
	}

	if (LogEnable) printf("%s", PBPrint(pb, pb->ioTrap|0xa000, err));
	return err;
}

int DriverStatus(CntrlParam *pb) {
	if (LogEnable) printf("%s", PBPrint(pb, pb->ioTrap|0xa000, 1));

	int err = statusErr;
	if (pb->csCode == kDriverGestaltCode) {
		DriverGestaltParam *gpb = (DriverGestaltParam *)pb;
		if (gpb->driverGestaltSelector == 'nmrg') {
			err = dgNameRegistryEntry(gpb);
		} else if (gpb->driverGestaltSelector == 'ofpt' || gpb->driverGestaltSelector == 'ofbt') {
			err = dgOpenFirmwareBoot(gpb);
		} else if (gpb->driverGestaltSelector == 'boot') {
			err = dgBoot(gpb);
		} else if (gpb->driverGestaltSelector == 'dvrf') {
			err = dgDeviceReference(gpb);
		} else if (gpb->driverGestaltSelector == 'intf') {
			err = dgInterface(gpb);
		} else if (gpb->driverGestaltSelector == 'devt') {
			err = dgDeviceType(gpb);
		}
	}

	if (LogEnable) printf("%s", PBPrint(pb, pb->ioTrap|0xa000, err));
	return err;
}
