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

static OSStatus finalize(DriverFinalInfo *info);
static OSStatus initialize(DriverInitInfo *info);
static OSErr ioCall(struct IOParam *pb);
static void installDrive(struct DrvQEl *drive, short driverRef);
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
static RegEntryID regentryid;
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

OSStatus DoDriverIO(AddressSpaceID spaceID, IOCommandID cmdID,
	IOCommandContents pb, IOCommandCode code, IOCommandKind kind) {
	OSStatus err;

	if (code <= 6) {
		printf("%s", PBPrint(pb.pb, (*pb.pb).ioParam.ioTrap | 0xa000, 1));
	}

	switch (code) {
	case kInitializeCommand:
	case kReplaceCommand:
		err = initialize(pb.initialInfo);
		break;
	case kFinalizeCommand:
	case kSupersededCommand:
		err = finalize(pb.finalInfo);
		break;
	case kControlCommand:
	case kStatusCommand:
		err = controlStatusCall(&(*pb.pb).cntrlParam);
		break;
	case kOpenCommand:
	case kCloseCommand:
		err = noErr;
		break;
	case kReadCommand:
		err = ioCall(&(*pb.pb).ioParam);
		break;
	case kWriteCommand:
		err = writErr;
		break;
	default:
		err = paramErr;
		break;
	}

	if (code <= 6) {
		printf("%s", PBPrint(pb.pb, (*pb.pb).ioParam.ioTrap | 0xa000, err));
	}

	// Return directly from every call
	if (kind & kImmediateIOCommandKind) {
		return err;
	} else {
		return IOCommandIsComplete(cmdID, err);
	}
}

static OSStatus finalize(DriverFinalInfo *info) {
	return noErr;
}

static OSStatus initialize(DriverInitInfo *info) {
	drvrRefNum = info->refNum;
	regentryid = info->deviceEntry;
	InitLog();
	sprintf(LogPrefix, "Block(%d) ", info->refNum);

	if (!VInit(&info->deviceEntry)) {
		printf("Transport layer failure\n");
		VFail();
		return openErr;
	};

	dqe.writeProt = 0x80 * VGetDevFeature(5); // VIRTIO_BLK_F_RO
	VSetFeature(5, VGetDevFeature(5)); // SHOULD use feature if offered
	if (!VFeaturesOK()) {
		printf("Feature negotiation failure\n");
		VFail();
		return openErr;
	}

	fixedbuf = AllocPages(1, &pfixedbuf);
	if (fixedbuf == NULL) {
		printf("Memory allocation failure\n");
		VFail();
		return openErr;
	}

	VDriverOK();

	buffers = QInit(0, MAXBUFFERS);
	if (buffers < 4) {
		printf("Virtqueue layer failure\n");
		VFail();
		return openErr;
	}
	QInterest(0, 1); // enable interrupts

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

	installDrive((void *)&dqe.qLink, drvrRefNum);
	if (*(char *)0x14a >= 0) { // check whether Event Mgr is actually up
		PostEvent(diskEvt, dqe.dQDrive);
	}

	printf("Ready\n");
	return noErr;
}

static OSErr ioCall(struct IOParam *pb) {
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

		volatile bool waiting = true;
		QSend(0, 1/*n_out*/, n+1/*n_in*/, phys, size, &waiting);
		QNotify(0);
		while (waiting) {}

		if (fixedbuf->reply != 0) {
			panic("bad reply");
		}

		for (int i=1; i<=n; i++) {
			pb->ioActCount += memblocks[i].count;
		}
	}
	UnlockMemory(pb->ioBuffer, pb->ioReqCount);
	return noErr;
}

static void installDrive(struct DrvQEl *drive, short driverRef) {
	short driveNum = 8; // conventional lowest number for HD
	while (findDrive(driveNum) != NULL) driveNum++;
	AddDrive(driverRef, driveNum, drive);
}

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
	pb.ioPosOffset = 512L * which;
	pb.ioReqCount = 512;
	pb.ioBuffer = fixedbuf->sector;
	ioCall(&pb);
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

void DNotified(uint16_t q, size_t len, void *tag) {
	*(volatile bool *)tag = false; // clear "waiting"
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
	pb->driverGestaltResponse = (long)&regentryid;
	return noErr;
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

static OSErr controlStatusCall(struct CntrlParam *pb) {
	// Coerce csCode or driverGestaltSelector into one long
	// Negative is Status/DriverGestalt, positive is Control/DriverConfigure
	// (Assume 4-byte Driver Gestalt code is ASCII, and therefore positive)
	long selector = pb->csCode;

	if (selector == kDriverGestaltCode)
		selector = ((struct DriverGestaltParam *)pb)->driverGestaltSelector;

	if ((pb->ioTrap & 0xff) == (_Status & 0xff))
		selector = -selector;

	return controlStatusDispatch(selector, pb);
}

static OSErr controlStatusDispatch(long selector, void *pb) {
	// (+) means Control, (-) means Status
	switch (selector) {
	case +kDriveIcon: return cIcon(pb);
	case +kMediaIcon: return cIcon(pb);
	case +kDriveInfo: return cDriveInfo(pb);
	case -kDriveStatus: return sDriveStatus(pb);
	case -'nmrg': return dgNameRegistryEntry(pb);
	case -'ofpt': case -'ofbt': return dgOpenFirmwareBoot(pb);
	case -'boot': return dgBoot(pb);
 	case -'dvrf': return dgDeviceReference(pb);
	case -'intf': return dgInterface(pb);
 	case -'devt': return dgDeviceType(pb);
	default:
		if (selector > 0) {
			return controlErr;
		} else {
			return statusErr;
		}
	}
}
