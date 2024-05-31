/* Copyright (c) 2023 Elliot Nunn */
/* Licensed under the MIT license */

/*
Driver for virtio-9p under the Macintosh File Manager

The biggest challenge is that 9P (and Unix in general) only allows file access
via a known path: supplying the inode number isn't enough.

To allow MacOS clients to access a file by number (called a CNID in HFS), we
maintain a giant SQLite database with this and other information.

The "File System Manager" (a convenience layer on top of the File Manager) is
not used because it is unavailable at the start of the boot process.

9P FID allocation:
0-31 are auto-closed by 9p.c when re-use is attempted
0 root
1-7 misc use by various functions in this file
8-15 multifork file access layer
16 browse catalog layer
*/

#include <Disks.h>
#include <DriverGestalt.h>
#include <DriverServices.h>
#include <Files.h>
#include <FSM.h>
#include <Gestalt.h>
#include <LowMem.h>
#include <Memory.h>
#include <MixedMode.h>
#include <OSUtils.h>
#include <Start.h>
#include <Traps.h>

#include "callin68k.h"
#include "catabrowse.h"
#include "device.h"
#include "metadb-glue.h"
#include "multifork.h"
#include "printf.h"
#include "panic.h"
#include "paramblkprint.h"
#include "patch68k.h"
#include "9p.h"
#include "sqlite3.h"
#include "timing.h"
#include "transport.h"
#include "unicode.h"
#include "universalfcb.h"
#include "virtqueue.h"

#include <stdbool.h> // leave till last, conflicts with Universal Interfaces
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define c2pstr(p, c) {uint8_t l=strlen(c); p[0]=l; memcpy(p+1, c, l);}
#define p2cstr(c, p) {uint8_t l=p[0]; memcpy(c, p+1, l); c[l]=0;}
#define pstrcpy(d, s) memcpy(d, s, 1+(unsigned char)s[0])

#define unaligned32(ptr) (((uint32_t)*(uint16_t *)(ptr) << 16) | *((uint16_t *)(ptr) + 1))

// rename some FCB fields for our own use
#define fcb9Link fcbFlPos
#define fcb9Opaque fcbExtRec // array totalling to 12 bytes

enum {
	// FSID is used by the ExtFS hook to version volume and drive structures,
	// so if the dispatch mechanism changes, this constant must change:
	FSID = ('9'<<8) | 'p',
	ROOTFID = 0,
	FID1 = 1,
	FID2 = 2,
	FID3 = 3,
	FIDPERSIST = 4,
	WDLO = -32767,
	WDHI = -4096,
	STACKSIZE = 256 * 1024, // large stack bc memory is so hard to allocate
};

struct longdqe {
	char writeProt; // bit 7 = 1 if volume is locked
	char diskInPlace; // 0 = no disk place, 1 or 2 = disk in place
	char installed; // 0 = don't know, 1 = installed, -1 = not installed
	char sides; // -1 = 2-sided, 0 = 1-sided
	DrvQEl dqe; // AddDrive points here
	void *dispatcher;
};

static OSStatus finalize(DriverFinalInfo *info);
static OSStatus initialize(DriverInitInfo *info);
static void installDrive(void);
static void installExtFS(void);
static void ensureFutureMount(void);
static void lateBootHook(void);
static void getBootBlocks(void);
static void setDirPBInfo(struct DirInfo *pb, int32_t cnid, uint32_t fid);
static void setFilePBInfo(struct HFileInfo *pb, int32_t cnid, uint32_t fid);
static int32_t pbDirID(void *_pb);
static struct WDCBRec *findWD(short refnum);
static int32_t qid2cnid(struct Qid9 qid);
static struct Qid9 qidTypeFix(struct Qid9 qid, char linuxType);
static bool iserr(int32_t cnid);
static bool isdir(int32_t cnid);
static void cnidPrint(int32_t cnid);
static struct DrvQEl *findDrive(short num);
static struct VCB *findVol(short num);
static void pathSplitLeaf(const unsigned char *path, unsigned char *dir, unsigned char *name);
static bool visName(const char *name);
static void startDB(void);
static void setDB(int32_t cnid, int32_t pcnid, const char *name);
static const char *getDBName(int32_t cnid);
static int32_t getDBParent(int32_t cnid);
int32_t mactime(int64_t unixtime);
static long fsCall(void *pb, long selector);
static OSErr fsDispatch(void *pb, unsigned short selector);
static OSErr controlStatusCall(struct CntrlParam *pb);
static OSErr controlStatusDispatch(long selector, void *pb);

static unsigned long hfsTimer, browseTimer, relistTimer;
static short drvrRefNum;
extern struct Qid9 root;
static char bootBlocks[1024];

static struct longdqe dqe = {
	.writeProt = 0,
	.diskInPlace = 8, // ???
	.installed = 1,
	.sides = 0,
	.dqe = {.dQFSID = FSID},
	.dispatcher = CALLIN68K_C_ARG44_GLOBDEF(fsCall), // procedure for our ToExtFS patch
};
static struct VCB vcb = {
	.vcbAtrb = 0x0000, // no locks
	.vcbSigWord = kHFSSigWord,
	.vcbNmFls = 1234,
	.vcbNmRtDirs = 6, // "number of directories in root" -- why?
	.vcbNmAlBlks = 0x7fff,
	.vcbAlBlkSiz = 32*1024,
	.vcbClpSiz = 32*1024,
	.vcbNxtCNID = 16, // the first "user" cnid... we will never use this field
	.vcbFreeBks = 0x7fff,
	.vcbFSID = FSID,
	.vcbFilCnt = 1,
	.vcbDirCnt = 1,
	.vcbCtlBuf = CALLIN68K_C_ARG44_GLOBDEF(fsCall), // overload field with proc pointer
};
static struct GetVolParmsInfoBuffer vparms = {
	.vMVersion = 1, // goes up to version 4
	.vMAttrib = 0
		| (1<<bNoMiniFndr)
		| (1<<bNoLclSync)
		| (1<<bTrshOffLine)
		| (1<<bHasExtFSVol)
		| (1<<bLocalWList)
		,
	.vMServerAdr = 0, // might be used for uniqueness checking -- ?set uniq
};

DriverDescription TheDriverDescription = {
	kTheDescriptionSignature,
	kInitialDriverDescriptor,
	{"\x0cpci1af4,1009", {0x00, 0x10, 0x80, 0x00}}, // v0.1
	{kDriverIsLoadedUponDiscovery |
		kDriverIsOpenedUponLoad,
		"\x09.Virtio9P"},
	{1, // nServices
	{{kServiceCategoryNdrvDriver, kNdrvTypeIsGeneric, {0x00, 0x10, 0x80, 0x00}}}} //v0.1
};

RegEntryID regentryid;

OSStatus DoDriverIO(AddressSpaceID spaceID, IOCommandID cmdID,
	IOCommandContents pb, IOCommandCode code, IOCommandKind kind) {
	OSStatus err;

	if (code <= 6 && logenable)
		printf("Drvr_%s", PBPrint(pb.pb, (*pb.pb).ioParam.ioTrap | 0xa000, 1));

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
	case kReadCommand: {
		struct IOParam *param = &pb.pb->ioParam;
		param->ioActCount = param->ioReqCount;
		for (long i=0; i<param->ioReqCount; i++) {
			if (param->ioPosOffset+i < sizeof bootBlocks) {
				param->ioBuffer[i] = bootBlocks[i];
			} else {
				param->ioBuffer[i] = 0;
			}
		}
		err = noErr;
		break;
		}
	default:
		err = paramErr;
		break;
	}

	if (code <= 6 && logenable)
		printf("%s", PBPrint(pb.pb, (*pb.pb).ioParam.ioTrap | 0xa000, err));

	// Return directly from every call
	if (kind & kImmediateIOCommandKind) {
		return err;
	} else {
		return IOCommandIsComplete(cmdID, err);
	}
}

void DNotified(uint16_t q, size_t len, void *tag) {
	QueueNotified9();
}

void DConfigChange(void) {
}


static OSStatus finalize(DriverFinalInfo *info) {
	return noErr;
}

static OSStatus initialize(DriverInitInfo *info) {
	// Debug output
	drvrRefNum = info->refNum;
	regentryid = info->deviceEntry;
	sprintf(logprefix, ".Virtio9P(%d) ", info->refNum);
// 	if (0 == RegistryPropertyGet(&info->deviceEntry, "debug", NULL, 0)) {
// 		logenable = 1;
// 	}

	printf("Starting\n");

	if (!VInit(&info->deviceEntry)) {
		printf("Transport layer failure\n");
		return openErr;
	};

	VSetFeature(0, 1); // Request mount_tag in the config area
	if (!VFeaturesOK()) {
		printf("Feature negotiation failure\n");
		VFail();
		return openErr;
	}

	// Cannot go any further without touching virtqueues, which requires DRIVER_OK
	VDriverOK();

	// Now is safe to allocate memory for the hash table
	HTallocate();

	// Request enough buffers to transfer a megabyte in page sized chunks
	uint16_t viobufs = QInit(0, 256);
	if (viobufs < 2) {
		printf("Virtqueue layer failure\n");
		VFail();
		return openErr;
	}
	QInterest(0, 1);

	// Start the 9P layer
	int err9;
	if ((err9 = Init9(viobufs)) != 0) {
		printf("9P layer failure\n");
		VFail();
		return openErr;
	}

	if ((err9 = Attach9(ROOTFID, (uint32_t)~0 /*auth=NOFID*/, "", "", 0, &root)) != 0) {
		return openErr;
	}

	// Start up the database for catalog IDs and other purposes
	int direrr = Mkdir9(ROOTFID, 0777, 0, ".classicvirtio.nosync.noindex", NULL);
	if (direrr && direrr!=EEXIST) panic("could not make work directory");
	direrr = Walk9(ROOTFID, 40000, 1, (const char *[]){".classicvirtio.nosync.noindex"}, NULL, NULL);
	if (direrr) panic("bad walk");

	startDB();

	// Read mount_tag from config space into a C string
	// (Suffixed with :1 or :2 etc to force a specific multifork format)
	long nameLen = *(unsigned char *)VConfig + 0x100 * *(unsigned char *)(VConfig+1);
	if (nameLen > 127) nameLen = 127;
	char name[128] = {};
	memcpy(name, VConfig+2, nameLen); // guarantee null term

	char *formathint = "";
	char *separator = strchr(name, '_');
	if (separator != NULL) {
		formathint = separator + 1;
		*separator = 0; // terminate the disk name there
	}

	printf("Volume name: %s\n", name);
	mr27name(vcb.vcbVN, name); // convert to short Mac Roman pascal string
	setDB(2, 1, name);

	// Need unique stable creation date, used pervasively as an ID, from inode#
	vcb.vcbCrDate = 0x80000000 ^
		(root.path & 0x3fffffff) ^
		((root.path >> 30) & 0x3fffffff) ^
		((root.path >> 60) & 0xf);

	// Choose a multifork format by probing the fs contents
	MFChoose(formathint);
	printf("Fork format: %s\n", MF.Name);
	if (MF.Init()) return memFullErr;

	installDrive();

	int32_t systemFolder = browse(FID1, 2 /*cnid*/, "\pSystem Folder");
	vcb.vcbFndrInfo[0] = iserr(systemFolder) ? 0 : systemFolder;
	printf("System Folder: %s\n", iserr(systemFolder) ? "absent" : "present");
	if (!iserr(systemFolder)) {
		getBootBlocks();
	}

	printf("File Manager: %s\n", GetVCBQHdr()->qHead != (void *)-1 ? "present" : "absent");

	if (GetVCBQHdr()->qHead != (void *)-1) {
		installExtFS();
		ensureFutureMount();
	} else {
		Patch68k(
			_InitFS,
			"4eb9 %o "   // jsr     originalInitFS
			"48e7 e0c0 " // movem.l d0-d2/a0-a1,-(sp)
			"4eb9 %l "   // jsr     installExtFS
			"4eb9 %l "   // jsr     ensureFutureMount
			"4cdf 0307", // movem.l (sp)+,d0-d2/a0-a1
			             // fallthru to uninstall code
			CALLIN68K_C_ARG0_FUNCDEF(installExtFS),
			CALLIN68K_C_ARG0_FUNCDEF(ensureFutureMount)
		);
	}

	return noErr;
}

// Does not require _InitFS to have been called
static void installDrive(void) {
	dqe.dqe.dQDrive = 8; // conventional lowest number for HD
	while (findDrive(dqe.dqe.dQDrive) != NULL) dqe.dqe.dQDrive++;
	AddDrive(drvrRefNum, dqe.dqe.dQDrive, &dqe.dqe);
	printf("Drive number: %d\n", dqe.dqe.dQDrive);
}

// Requires _InitFS to have been called
static void installExtFS(void) {
	// A single ToExtFS patch can be shared between multiple 9P device drivers
	// Use a Gestalt selector to declare its presence
	long selector = FSID<<16 | 'h'<<8 | 'k';
	void *patchaddr;
	if (Gestalt(selector, (long *)&patchaddr) != noErr) patchaddr = NULL;

	printf("Hooking ToExtFS (Gestalt '%.4s'): ", &selector);
	if (patchaddr) {
		printf("already installed at %p\n", patchaddr);
		return;
	}

	// External filesystems need a big stack, and they can't
	// share the FileMgr stack because of reentrancy problems
	// (Note that this is shared between .Virtio9P instances)
	char *stack = NewPtrSysClear(STACKSIZE);
	if (stack == NULL) panic("failed extfs stack allocation");

	// All instances of this driver share the one 68k hook (and one stack)
	patchaddr = Patch68k(
		0x3f2, // ToExtFS:
		// Fast path is when ReqstVol points to a 9p device (inspect VCB)
		// (ReqstVol can be -1 on a Quadra in early boot)
		"2438 03ee "      // move.l  ReqstVol,d2
		"6f %MOUNTCK "    // ble.s   MOUNTCK
		"2242 "           // move.l  d2,a1
		"0c69 %w 004c "   // cmp.w   #FSID,vcbFSID(a1)
		"66 %PUNT "       // bne.s   PUNT

		"2429 00a8 "      // move.l  vcbCtlBuf(a1),d2

		// Commit to calling the 9p routine (d2)
		"GO: "
		"2f38 0110 "      // move.l  StkLowPt,-(sp)
		"42b8 0110 "      // clr.l   StkLowPt
		"43f9 %l "        // lea.l   stack,a1
		"c34f"            // exg     a1,sp
		"2f09 "           // move.l  a1,-(sp)
		"2f00 "           // move.l  d0,-(sp)
		"2f08 "           // move.l  a0,-(sp)
		"2042 "           // move.l  d2,a0
		"4e90 "           // jsr     (a0)
		"2e6f 0008"       // move.l  8(sp),sp
		"21df 0110 "      // move.l  (sp)+,StkLowPt
		"4e75 "           // rts

		// Slow path is MountVol on a 9p device (find and inspect DQE)
		"MOUNTCK: "
		"0c28 000f 0007 " // cmp.b   #_MountVol&255,ioTrap+1(a0)
		"66 %PUNT "       // bne.s   PUNT

		"43f8 030a "      // lea     DrvQHdr+QHead,a1
		"LOOP: "
		"2251 "           // move.l  (a1),a1
		"2409 "           // move.l  a1,d2
		"67 %PUNT "       // beq.s   PUNT
		"3428 0016 "      // move.w  ioVRefNum(a0),d2
		"b469 0006 "      // cmp.w   dQDrive(a1),d2
		"66 %LOOP "       // bne.s   LOOP

		// Found matching drive, is it one of ours?
		"2429 %w "        // move.l  dispatcher(a1),d2
		"0c69 %w 000a "   // cmp.w   #FSID,dQFSID(a1)
		"67 %GO "         // beq.s   GO

		"PUNT: "
		"4ef9 %o ",       // jmp     original

		FSID,
		stack + STACKSIZE - 100,
		offsetof(struct longdqe, dispatcher) - offsetof(struct longdqe, dqe),
		FSID
	);

	// Use this single code path instead of SetGestaltValue
	printf("Setting Gestalt '%.4s' to %p: ", &selector, patchaddr);
	Patch68k(
		selector,
		"205f "    // move.   (sp)+,a0 ; a0 = return address
		"225f "    // move.   (sp)+,a1 ; a1 = address to return result
		"584f "    // addq    #4,sp    ; selector, discard
		"4257 "    // clr.w   (sp)     ; return value = noErr
		"22bc %l " // move.l  #value,(a1)
		"4ed0",    // jmp     (a0)
		patchaddr
	);
}

// Conventionally, posting a diskEvt some time after InitEvents was thought
// to ensure an eventual MountVol. But OS 9 seems to lose events posted early
// in the startup process, so we just call MountVol when startup is complete.
// (TN1189 suggests repeatedly posting diskEvt at accRun time, but this would
// need an NDRV compatible accRun substitute. Our way is simpler.)
static void ensureFutureMount(void) {
	PostEvent(diskEvt, dqe.dqe.dQDrive);
	printf("Posted (diskEvt,%d) and will call MountVol after startup:\n", dqe.dqe.dQDrive);

	// Process Manager calls NewGestalt('os  ') at the very end of startup
	Patch68k(
		_Gestalt,
		"0c80 6f732020" //      cmp.l   #'os  ',d0
		"661c"          //      bne.s   old
		"0801 0009"     //      btst    #9,d1
		"6716"          //      beq.s   old
		"0801 000a"     //      btst    #10,d1
		"6610"          //      bne.s   old
		"48e7 e0e0"     //      movem.l d0-d2/a0-a2,-(sp)
		"4eb9 %l"       //      jsr     lateBootHook
		"4cdf 0707"     //      movem.l (sp)+,d0-d2/a0-a2
		"6106"          //      bsr.s   uninstall
		"4ef9 %o",      // old: jmp     originalGestalt
		                // uninstall: (fallthrough code)
		CALLIN68K_C_ARG0_FUNCDEF(lateBootHook)
	);
}

static void lateBootHook(void) {
	if (dqe.dqe.qType == 0) {
		struct IOParam pb = {.ioVRefNum = dqe.dqe.dQDrive};
		PBMountVol((void *)&pb);
	}
}

// I want to be able to boot from a folder without MacOS "blessing" it
// so I need to find the System file, extract boot 1 resource, and close it.
// At this stage we are a drive, not a volume, so there is only block-level access
// ... but we need to access a resource fork ... so do some funky stuff.
static void getBootBlocks(void) {
	int32_t cnid = browse(FID1, vcb.vcbFndrInfo[0] /*system folder*/, "\pSystem");
	if (iserr(cnid)) return;

	uint64_t opaque[3] = {};
	int err = MF.Open(opaque, 0 /*unrealisted refnum*/, cnid, FID1, getDBName(cnid), true /*rfork*/, false /*write*/);
	if (err) return;

	uint32_t content;
	if (MF.Read(opaque, &content, 0, 4, NULL)) return;

	uint32_t map;
	if (MF.Read(opaque, &map, 4, 4, NULL)) return;

	uint16_t tloffset; // type list
	if (MF.Read(opaque, &tloffset, map + 24, 2, NULL)) return;
	uint32_t tl = map + tloffset;

	uint16_t nt;
	if (MF.Read(opaque, &nt, tl, 2, NULL)) return;
	nt++; // ffff means zero types

	for (uint16_t i=0; i<nt; i++) {
		uint32_t t = tl + 2 + 8*i;

		uint32_t tcode;
		if (MF.Read(opaque, &tcode, t, 4, NULL)) return;
		if (tcode != 'boot') continue;

		uint16_t nr; // n(resources of this type)
		uint16_t r1; // first resource of this type
		if (MF.Read(opaque, &nr, t+4, 2, NULL)) return;
		if (MF.Read(opaque, &r1, t+6, 2, NULL)) return;
		nr++; // "0" meant "one resource"

		for (uint16_t j=0; j<nr; j++) {
			uint32_t r = tl + r1 + 12*j;

			uint16_t id;
			if (MF.Read(opaque, &id, r, 2, NULL)) return;
			if (id != 1) continue;

			uint32_t off;
			if (MF.Read(opaque, &off, r+4, 4, NULL)) return;
			off &= 0xffffff;

			uint32_t len;
			if (MF.Read(opaque, &len, content+off, 4, NULL)) return;

			if (len > 1024) len = 1024;
			MF.Read(opaque, bootBlocks, content+off+4, len, NULL);

			return; // success
		}

		printf("%d resources of the relevant type\n", nr);
		panic("ok");
	}
}

static OSErr fsMountVol(struct IOParam *pb) {
	if (dqe.dqe.qType) return volOnLinErr;

	vparms.vMLocalHand = NewHandleSysClear(2);

	vcb.vcbDrvNum = dqe.dqe.dQDrive;
	vcb.vcbDRefNum = drvrRefNum;
	vcb.vcbVRefNum = -1;

	while (findVol(vcb.vcbVRefNum) != NULL) vcb.vcbVRefNum--;

	if (GetVCBQHdr()->qHead == NULL) {
		*(short *)0x352 = (long)&vcb >> 16; // DefVCBPtr
		*(short *)0x354 = (long)&vcb;
		*(short *)0x384 = vcb.vcbVRefNum; // DefVRefNum

		memcpy(findWD(0),
			&(struct WDCBRec){.wdVCBPtr=&vcb, .wdDirID = 2},
			16);
	}

	Enqueue((QElemPtr)&vcb, GetVCBQHdr());

	// Hack to show this volume in the Startup Disk cdev
	dqe.dqe.dQFSID = 0;

	return noErr;
}

static OSErr fsGetVolInfo(struct XVolumeParam *pb) {
	// Sizes
	struct Statfs9 statfs = {};
	Statfs9(ROOTFID, &statfs);
	uint64_t total = statfs.blocks * statfs.bsize;
	uint64_t free = statfs.bavail * statfs.bsize;

	if ((pb->ioTrap&0x00ff) == 0x60) { // XGetVolInfo
		pb->ioVTotalBytes = total;
		pb->ioVFreeBytes = free;
	}

	// Clip sizes to less than 2 GB, reported as 32k blocks
	if (total > 0x7fffffff) total = 0x7fffffff;
	if (free > 0x7fffffff) free = 0x7fffffff;
	vcb.vcbNmAlBlks = pb->ioVNmAlBlks = total >> 15;
	vcb.vcbFreeBks = pb->ioVFrBlk = total >> 15;

	// Get root dir modification date
	struct Stat9 stat = {};
	Getattr9(ROOTFID, STAT_MTIME, &stat);
	vcb.vcbLsMod = pb->ioVLsMod = mactime(stat.mtime_sec);

	// Allow working directories to pretend to be disks
	int32_t cnid = 2;
	if (pb->ioVRefNum <= WDHI) {
		struct WDCBRec *wdcb = findWD(pb->ioVRefNum);
		if (wdcb) cnid = wdcb->wdDirID;
	}

	// Count contained files
	pb->ioVNmFls = 0;

	int err = browse(FID1, cnid, "");
	if (err < 0) return err;

	if (Lopen9(FID1, O_RDONLY|O_DIRECTORY, NULL, NULL)) return noErr;

	char scratch[4096];
	InitReaddir9(FID1, scratch, sizeof scratch);

	char type;
	char childname[512];
	while (Readdir9(scratch, NULL, &type, childname) == 0) {
		if (visName(childname) && type != 4 /*not folder*/) pb->ioVNmFls++;
	}

	vcb.vcbNmFls = pb->ioVNmFls;

	return noErr;
}

static OSErr fsGetVolParms(struct HIOParam *pb) {
	short s = pb->ioReqCount;
	if (s > 14) s = 14; // not the whole struct, just the v1 part
	memcpy(pb->ioBuffer, &vparms, s);
	pb->ioActCount = s;
	return noErr;
}

// Files:                                   Directories:
// -->    12    ioCompletion   pointer      -->    12    ioCompletion  pointer
// <--    16    ioResult       word         <--    16    ioResult      word
// <->    18    ioNamePtr      pointer      <->    18    ioNamePtr     pointer
// -->    22    ioVRefNum      word         -->    22    ioVRefNum     word
// <--    24    ioFRefNum      word         <--    24    ioFRefNum     word (ERROR: NOT SET FOR DIRS)
// -->    28    ioFDirIndex    word         -->    28    ioFDirIndex   word
// <--    30    ioFlAttrib     byte         <--    30    ioFlAttrib    byte
// <--    31    ioACUser       byte         access rights for directory only
// <--    32    ioFlFndrInfo   16 bytes     <--    32    ioDrUsrWds    16 bytes
// <->    48    ioDirID        long word    <->    48    ioDrDirID     long word
// <--    52    ioFlStBlk      word         <--    52    ioDrNmFls     word
// <--    54    ioFlLgLen      long word
// <--    58    ioFlPyLen      long word
// <--    62    ioFlRStBlk     word
// <--    64    ioFlRLgLen     long word
// <--    68    ioFlRPyLen     long word
// <--    72    ioFlCrDat      long word    <--    72    ioDrCrDat    long word
// <--    76    ioFlMdDat      long word    <--    76    ioDrMdDat    long word
// <--    80    ioFlBkDat      long word    <--    80    ioDrBkDat    long word
// <--    84    ioFlXFndrInfo  16 bytes     <--    84    ioDrFndrInfo 16 bytes
// <--    100   ioFlParID      long word    <--    100    ioDrParID   long word
// <--    104   ioFlClpSiz     long word

static OSErr fsGetFileInfo(struct HFileInfo *pb) {
	bool catalogCall = (pb->ioTrap&0x00ff) == 0x0060; // GetCatInfo

	int idx = pb->ioFDirIndex;
	if (idx<0 && !catalogCall) idx=0; // make named GetFInfo calls behave right

	int32_t cnid = pbDirID(pb);

	if (idx > 0) {
		printf("Find by: directory+index\n");

		// Software commonly calls with index 1, 2, 3 etc
		// Cache Readdir9 to avoid quadratically relisting the directory per-call
		// An improvement might be to have multiple caches
		static char scratch[2048];
		static long lastCNID;
		static int lastIdx;

		// Invalidate the cache (by setting lastCNID to 0)
		if (cnid != lastCNID || idx <= lastIdx) {
			lastCNID = 0;
			lastIdx = 0;
			if (iserr(browse(FIDPERSIST, cnid, ""))) return fnfErr;
			if (Lopen9(FIDPERSIST, O_RDONLY|O_DIRECTORY, NULL, NULL)) return permErr;
			InitReaddir9(FIDPERSIST, scratch, sizeof scratch);
			lastCNID = cnid;
		}

		char name[512];
		struct Qid9 qid;

		// Fast-forward
		while (lastIdx < idx) {
			char type;
			int err = Readdir9(scratch, &qid, &type, name);
			qid = qidTypeFix(qid, type);

			if (err) {
				lastCNID = 0;
				Clunk9(FIDPERSIST);
				return fnfErr;
			}

			// GetFileInfo/HGetFileInfo ignores child directories
			// Note that Rreaddir does return a qid, but the type field of that
			// qid is unpopulated. So we use the Linux-style type byte instead.
			if ((!catalogCall && type == 4) || !visName(name)) {
				continue;
			}

			lastIdx++;
		}

		int32_t childcnid = qid2cnid(qid);
		setDB(childcnid, cnid, name);
		cnid = childcnid;

		browse(FID1, cnid, "");
	} else if (idx == 0) {
		printf("Find by: directory+path\n");
		cnid = browse(FID1, cnid, pb->ioNamePtr);
		if (iserr(cnid)) return cnid;
	} else {
		printf("Find by: directory only\n");
		cnid = browse(FID1, cnid, "\p");
		if (iserr(cnid)) return cnid;
	}

	if (logenable) {
		printf("Found: "); cnidPrint(cnid); printf("\n");
	}

	// A special return field: don't change the field, just follow the pointer
	if ((idx != 0) && (pb->ioNamePtr != NULL)) {
		mr31name(pb->ioNamePtr, getDBName(cnid));
	}

	if (isdir(cnid)) {
		if (!catalogCall) return fnfErr; // GetFileInfo predates directories
		setDirPBInfo((void *)pb, cnid, FID1);
	} else {
		setFilePBInfo((void *)pb, cnid, FID1);
	}

	return noErr;
}

static void setDirPBInfo(struct DirInfo *pb, int32_t cnid, uint32_t fid) {
	// 9P/Unix lack a call to count the contents of a directory, so list it
	int valence=0;
	char childname[512];
	char scratch[4096];

	Walk9(fid, FID1, 0, NULL, NULL, NULL);
	Lopen9(FID1, O_RDONLY|O_DIRECTORY, NULL, NULL);
	InitReaddir9(FID1, scratch, sizeof scratch);
	while (Readdir9(scratch, NULL, NULL, childname) == 0 && valence < 0x7fff) {
		if (visName(childname)) valence++;
	}
	Clunk9(FID1);

	struct MFAttr attr;
	MF.DGetAttr(cnid, fid, getDBName(cnid), MF_FINFO, &attr);

	// Clear fields from ioFlAttrib onward
	memset((char *)pb + 30, 0, 100 - 30);

	pb->ioFRefNum = 0; // not sure what this means for dirs?
	pb->ioFlAttrib = ioDirMask;
	memcpy(&pb->ioDrUsrWds, attr.finfo, sizeof pb->ioDrUsrWds);
	pb->ioDrDirID = cnid;
	pb->ioDrNmFls = valence;
	pb->ioDrCrDat = pb->ioDrMdDat = mactime(attr.unixtime);
	memcpy(&pb->ioDrFndrInfo, attr.fxinfo, sizeof pb->ioDrFndrInfo);
	pb->ioDrParID = getDBParent(cnid);
}

static void setFilePBInfo(struct HFileInfo *pb, int32_t cnid, uint32_t fid) {
	struct MFAttr attr;
	MF.FGetAttr(cnid, fid, getDBName(cnid), MF_DSIZE|MF_RSIZE|MF_TIME|MF_FINFO, &attr);

	// Determine whether the file is open
	bool openRF = false, openDF = false;
	short openAs = 0, refnum = 0;
	FCBRec *fcb = NULL;
	while (UnivIndexFCB(&vcb, &refnum, &fcb) == noErr) {
		if (fcb->fcbFlNm != cnid) continue;

		openAs = refnum;
		if (fcb->fcbFlags & fcbResourceMask) {
			openRF = true;
		} else {
			openDF = true;
		}
	}

	// Clear shared "FileInfo" fields, from ioFlAttrib onward
	memset((char *)pb + 30, 0, 80 - 30);

	pb->ioFRefNum = openAs;
	pb->ioFlAttrib =
		(kioFlAttribResOpenMask * openRF) |
		(kioFlAttribDataOpenMask * openDF) |
		(kioFlAttribFileOpenMask * (openRF || openDF));
	memcpy(&pb->ioFlFndrInfo, attr.finfo, sizeof pb->ioFlFndrInfo);
	if (pb->ioTrap & 0x200) pb->ioDirID = cnid; // peculiar field
	pb->ioFlLgLen = attr.dsize;
	pb->ioFlPyLen = (attr.dsize + 511) & -512;
	pb->ioFlRLgLen = attr.rsize;
	pb->ioFlRPyLen = (attr.rsize + 511) & -512;
	pb->ioFlCrDat = pb->ioFlMdDat = mactime(attr.unixtime);

	if ((pb->ioTrap & 0xff) != 0x60) return;
	// GetCatInfo only beyond this point

	// Clear only "CatInfo" fields, from ioFlBkDat onward
	memset((char *)pb + 80, 0, 108 - 80);

	pb->ioFlParID = getDBParent(cnid);
}

// Set creator and type on files only
// TODO set timestamps, the attributes byte (comes with AppleDouble etc)
static OSErr fsSetFileInfo(struct HFileInfo *pb) {
	int32_t cnid = pbDirID(pb);
	cnid = browse(FID1, cnid, pb->ioNamePtr);
	if (iserr(cnid)) return cnid;

	// TODO: mtime setting
	struct MFAttr attr = {};
	memcpy(attr.finfo, &pb->ioFlFndrInfo, sizeof pb->ioFlFndrInfo); // same field as ioDrUsrWds
	memcpy(attr.fxinfo, &pb->ioFlXFndrInfo, sizeof pb->ioFlXFndrInfo); // same field as ioDrFndrInfo

	if (isdir(cnid)) {
		MF.DSetAttr(cnid, FID1, getDBName(cnid), MF_FINFO, &attr);
	} else {
		MF.FSetAttr(cnid, FID1, getDBName(cnid), MF_FINFO, &attr);
	}

	return noErr;
}

static OSErr fsSetVol(struct HFileParam *pb) {
	struct VCB *setDefVCBPtr;
	short setDefVRefNum;
	struct WDCBRec setDefWDCB;

	if (pb->ioTrap & 0x200) {
		// HSetVol: any directory is fair game,
		// so check that the path exists and is really a directory
		int32_t cnid = browse(FID1, pbDirID(pb), pb->ioNamePtr);
		if (iserr(cnid)) return cnid;
		if (!isdir(cnid)) return dirNFErr;
		Clunk9(FID1);

		setDefVCBPtr = &vcb;
		setDefVRefNum = vcb.vcbVRefNum;
		setDefWDCB = (struct WDCBRec){
			.wdVCBPtr=&vcb,
			.wdDirID=cnid
		};
	} else {
		// SetVol: only the root or a Working Directory is possible,
		// and in either case the directory is known already to exist
		if (pb->ioVRefNum <= WDHI) { // Working Directory
			setDefVCBPtr = &vcb;
			setDefVRefNum = pb->ioVRefNum;
			setDefWDCB = (struct WDCBRec){
				.wdVCBPtr=&vcb,
				.wdDirID=findWD(pb->ioVRefNum)->wdDirID
			};
		} else { // Root (via path, volume number or drive number)
			setDefVCBPtr = &vcb;
			setDefVRefNum = vcb.vcbVRefNum;
			setDefWDCB = (struct WDCBRec){
				.wdVCBPtr=&vcb,
				.wdDirID=2
			};
		}
	}

	// Set super secret globals
	*(short *)0x352 = (long)setDefVCBPtr >> 16;
	*(short *)0x354 = (long)setDefVCBPtr;
	*(short *)0x384 = setDefVRefNum;
	memcpy(findWD(0), &setDefWDCB, sizeof setDefWDCB);
	return noErr;
}

static OSErr fsMakeFSSpec(struct HIOParam *pb) {
	struct FSSpec *spec = (struct FSSpec *)pb->ioMisc;

	int32_t cnid = pbDirID(pb);
	cnid = browse(FID1, cnid, pb->ioNamePtr);
	if (!iserr(cnid)) {
		// The target exists
		if (cnid == 2) {
			spec->vRefNum = vcb.vcbVRefNum;
			spec->parID = 2;
			spec->name[0] = 0;
		} else {
			spec->vRefNum = vcb.vcbVRefNum;
			spec->parID = getDBParent(cnid);
			mr31name(spec->name, getDBName(cnid));
		}

		return noErr;
	}

	// The target doesn't (yet) exist
	unsigned char path[256], leaf[256];
	if (pb->ioNamePtr == NULL) return dirNFErr;
	pathSplitLeaf(pb->ioNamePtr, path, leaf);
	if (leaf[0] == 0) return dirNFErr;

	cnid = pbDirID(pb);
	cnid = browse(FID1, cnid, path);
	if (iserr(cnid)) return dirNFErr; // return cnid;

	spec->vRefNum = vcb.vcbVRefNum;
	spec->parID = cnid;
	pstrcpy(spec->name, leaf);

	return fnfErr;
}

// Update the EOF ofall duplicate FCBs
// following the linked list set up in fsOpen
static void updateKnownLength(short refnum, int32_t length) {
	short fullcircle = refnum;
	for (;;) {
		FCBRec *fcb;
		if (UnivResolveFCB(refnum, &fcb)) panic("FCB linked list broken (RW)");

		fcb->fcbEOF = length;
		if (fcb->fcbCrPs > length) fcb->fcbCrPs = length;

		refnum = fcb->fcb9Link;
		if (refnum == fullcircle) break;
	}
}

static OSErr fsOpen(struct HIOParam *pb) {
// 	memcpy(LMGetCurrentA5() + 0x278, "CB", 2); // Force early MacsBug, TODO absolutely will crash

	pb->ioRefNum = 0;

	bool rfork = (pb->ioTrap & 0xff) == 0x0a;

	short refn;
	struct FCBRec *fcb;
	if (UnivAllocateFCB(&refn, &fcb) != noErr) return tmfoErr;

	int32_t cnid = pbDirID(pb);
	cnid = browse(FID1, cnid, pb->ioNamePtr);
	if (iserr(cnid)) return cnid;
	if (isdir(cnid)) return fnfErr;

	uint64_t opaque[3] = {};
	int err = MF.Open(opaque, refn, cnid, FID1, getDBName(cnid), rfork, pb->ioPermssn != fsRdPerm);
	// no way to tell if we were successful in getting write perms, but the File Manager API permits this
	if (err == EPERM) return permErr;
	else if (err == ENOENT) return fnfErr;
	else if (err) return ioErr;

	struct MFAttr attr = {};
	MF.FGetAttr(cnid, FID1, getDBName(cnid), MF_FINFO, &attr);

	uint64_t size;
	MF.GetEOF(opaque, &size);
	if (size > 0xfffffd00) size = 0xfffffd00;

	*fcb = (struct FCBRec){
		.fcbFlNm = cnid,
		.fcbFlags =
			(fcbResourceMask * rfork) |
			(fcbWriteMask * (pb->ioPermssn != fsRdPerm)),
		.fcbTypByt = 0, // MFS only
		.fcb9Link = refn,
		.fcbEOF = size,
		.fcbPLen = (size + 511) & -512,
		.fcbCrPs = 0,
		.fcbVPtr = &vcb,
		.fcbBfAdr = NULL, // reserved
		.fcbClmpSize = 512,
		.fcbBTCBPtr = NULL, // reserved
		.fcbCatPos = 0, // own use
		.fcbDirID = getDBParent(cnid),
	};
	memcpy(fcb->fcb9Opaque, opaque, 12); // special private struct
	memcpy(&fcb->fcbFType, attr.finfo, 4); // 4char type code
	mr31name(fcb->fcbCName, getDBName(cnid));

	// Arrange FCBs of the same fork into a circular linked list
	// Do a linear search for any other same-fork FCB
	short fellowFile = 0;
	FCBRec *fellowFCB;
	while (UnivIndexFCB(&vcb, &fellowFile, &fellowFCB) == noErr) {
		if (fellowFile == refn) continue; // can't be literally the same FCB
		if (fellowFCB->fcbFlNm != fcb->fcbFlNm) continue; // must be same CNID
		if ((fellowFCB->fcbFlags & fcbResourceMask) != (fcb->fcbFlags & fcbResourceMask)) continue; // must be same data/rsrc

		fcb->fcb9Link = fellowFCB->fcb9Link;
		fellowFCB->fcb9Link = refn;
		break;
	}

	pb->ioRefNum = refn;

	return noErr;
}

static OSErr fsGetEOF(struct IOParam *pb) {
	struct FCBRec *fcb;
	if (UnivResolveFCB(pb->ioRefNum, &fcb))
		return paramErr;

	uint64_t size;
	MF.GetEOF(fcb->fcb9Opaque, &size);
	if (size > 0xfffffd00) size = 0xfffffd00;

	fcb->fcbEOF = size;
	pb->ioMisc = (Ptr)(uint32_t)size;

	return noErr;
}

static OSErr fsSetEOF(struct IOParam *pb) {
	struct FCBRec *fcb;
	if (UnivResolveFCB(pb->ioRefNum, &fcb))
		return paramErr;

	long len = (uint32_t)pb->ioMisc;

	int err = MF.SetEOF(fcb->fcb9Opaque, len);
	if (err) panic("seteof error");

	updateKnownLength(pb->ioRefNum, len);

	return noErr;
}

static OSErr fsClose(struct IOParam *pb) {
	struct FCBRec *fcb;
	if (UnivResolveFCB(pb->ioRefNum, &fcb))
		return paramErr;

	// Remove this FCB from the linked list of this fork
	short fellowFile = pb->ioRefNum;
	FCBRec *fellowFCB;
	for (;;) {
		if (UnivResolveFCB(fellowFile, &fellowFCB) != noErr) panic("FCB linked list broken (Close)");

		if (fellowFCB->fcb9Link == pb->ioRefNum) {
			fellowFCB->fcb9Link = fcb->fcb9Link;
			break;
		}
		fellowFile = fellowFCB->fcb9Link;
	}

	MF.Close(fcb->fcb9Opaque);

	fcb->fcbFlNm = 0;

	return noErr;
}

static OSErr fsRead(struct IOParam *pb) {
	// Reads to ROM are get discarded
	char scratch[512];
	bool usescratch = ((uint32_t)pb->ioBuffer >> 16) >= *(uint16_t *)0x2ae; // ROMBase

	pb->ioActCount = 0;

	struct FCBRec *fcb;
	if (UnivResolveFCB(pb->ioRefNum, &fcb))
		return paramErr;

	// The seek/tell calls are just zero-length read calls
	if ((pb->ioTrap & 0xff) == (_GetFPos & 0xff)) {
		pb->ioPosMode = fsAtMark;
		pb->ioReqCount = 0;
	} else if ((pb->ioTrap & 0xff) == (_SetFPos & 0xff)) {
		pb->ioReqCount = 0;
	}

	int32_t start, end, pos;

	char seek = pb->ioPosMode & 3;
	if (seek == fsAtMark) {
		pos = start = fcb->fcbCrPs;
	} else if (seek == fsFromStart) {
		pos = start = pb->ioPosOffset;
	} else if (seek == fsFromLEOF) {
		// Check the on-disk EOF for concurrent modification
		uint64_t cursize;
		MF.GetEOF(fcb->fcb9Opaque, &cursize);
		updateKnownLength(pb->ioRefNum, cursize);
		pos = start = fcb->fcbEOF + pb->ioPosOffset;
	} else if (seek == fsFromMark) {
		pos = start = fcb->fcbCrPs + pb->ioPosOffset;
	}
	end = pos + pb->ioReqCount;

	// Cannot position before start of file (like OS 9, unlike OS 7)
	if (start < 0) {
		pb->ioPosOffset = fcb->fcbCrPs;
		return posErr;
	}

	// The zero-length case is likely GetFPos or SetFPos
	if (start == end) {
		if (start > fcb->fcbEOF) {
			pb->ioPosOffset = fcb->fcbCrPs = fcb->fcbEOF;
			return eofErr;
		} else {
			pb->ioPosOffset = fcb->fcbCrPs = start;
			return noErr;
		}
	}

	// Request the host
	while (pos != end) {
		int32_t want = end - pos;
		if (want > Max9) want = Max9;

		char *buf = pb->ioBuffer + pos - start;

		// Discard a read into a ROM-based buffer
		if (usescratch) {
			if (want > sizeof scratch) want = sizeof scratch;
			buf = scratch;
		}

		uint32_t got = 0;
		MF.Read(fcb->fcb9Opaque, buf, pos, want, &got);

		pos += got;
		if (got != want) break;
	}

	// File proves longer or shorter than expected
	if (pos > fcb->fcbEOF || pos < end) {
		updateKnownLength(pb->ioRefNum, pos);
	}

	pb->ioPosOffset = fcb->fcbCrPs = pos;
	pb->ioActCount = pos - start;
	if (pos != end) {
		return eofErr;
	} else {
		return noErr;
	}
}

static OSErr fsWrite(struct IOParam *pb) {
	// Writes from ROM are not supported by the 9P layer (fix this!)
	char scratch[512];
	bool usescratch = ((uint32_t)pb->ioBuffer >> 16) >= *(uint16_t *)0x2ae; // ROMBase

	pb->ioActCount = 0;

	struct FCBRec *fcb;
	if (UnivResolveFCB(pb->ioRefNum, &fcb))
		return paramErr;

	int32_t start, end, pos;

	char seek = pb->ioPosMode & 3;
	if (seek == fsAtMark) {
		pos = start = fcb->fcbCrPs;
	} else if (seek == fsFromStart) {
		pos = start = pb->ioPosOffset;
	} else if (seek == fsFromLEOF) {
		// Check the on-disk EOF for concurrent modification
		uint64_t cursize;
		MF.GetEOF(fcb->fcb9Opaque, &cursize);
		updateKnownLength(pb->ioRefNum, cursize);
		pos = start = fcb->fcbEOF + pb->ioPosOffset;
	} else if (seek == fsFromMark) {
		pos = start = fcb->fcbCrPs + pb->ioPosOffset;
	}
	end = pos + pb->ioReqCount;

	// Cannot position before start of file (like OS 9, unlike OS 7)
	if (start < 0) {
		pb->ioPosOffset = fcb->fcbCrPs;
		return posErr;
	}

	if (start > fcb->fcbEOF) {
		printf("Write at offset %d of %d byte file: OS 9 would write junk data!\n", start, fcb->fcbEOF);
	}

	// Request the host
	while (pos != end) {
		int32_t want = end - pos;
		if (want > Max9) want = Max9;

		char *buf = pb->ioBuffer + pos - start;

		// Copy the data into RAM before the 9P call
		if (usescratch) {
			if (want > sizeof scratch) want = sizeof scratch;
			BlockMoveData(buf, scratch, want);
			buf = scratch;
		}

		uint32_t got = 0;
		MF.Write(fcb->fcb9Opaque, buf, pos, want, &got);

		pos += got;
		if (got != want) panic("write call incomplete");
	}

	// File is now longer
	if (pos > fcb->fcbEOF) {
		updateKnownLength(pb->ioRefNum, pos);
	}

	pb->ioPosOffset = fcb->fcbCrPs = pos;
	pb->ioActCount = pos - start;
	return noErr;
}

static OSErr fsCreate(struct HFileParam *pb) {
	int err = browse(FID1, pbDirID(pb), pb->ioNamePtr);

	if (!iserr(err)) { // actually found a file
		return dupFNErr;
	} else if (err != fnfErr) {
		return err;
	}

	unsigned char dir[256], name[256];
	pathSplitLeaf(pb->ioNamePtr, dir, name);

	if (name[0] == 0) return bdNamErr;

	int32_t parentCNID = browse(FID1, pbDirID(pb), dir);

	if (iserr(parentCNID)) return dirNFErr;

	char uniname[1024];
	int n=0;
	for (int i=0; i<name[0]; i++) {
		long bytes = utf8char(name[i+1]);
		if (bytes == '/') bytes = ':';
		do {
			uniname[n++] = bytes;
			bytes >>= 8;
		} while (bytes);
	}
	uniname[n++] = 0;

	if ((pb->ioTrap & 0xff) == (_Create & 0xff)) {
		if (Lcreate9(FID1, O_CREAT|O_EXCL, 0666, 0, uniname, NULL, NULL) || Clunk9(FID1)) return ioErr;
	} else {
		struct Qid9 qid;
		if (Mkdir9(FID1, 0777, 0, uniname, &qid) || Clunk9(FID1)) return ioErr;

		// DirCreate returns DirID, and therefore we must put it in the database
		int32_t cnid = qid2cnid(qid);
		setDB(cnid, parentCNID, uniname);
		pb->ioDirID = cnid;
	}

	return noErr;
}

static OSErr fsDelete(struct IOParam *pb) {
	int32_t cnid = browse(FID1, pbDirID(pb), pb->ioNamePtr);
	if (iserr(cnid)) return cnid;

	// Do not allow removal of open files
	short openFork = 0;
	FCBRec *openFCB;
	while (UnivIndexFCB(&vcb, &openFork, &openFCB) == noErr) {
		if (openFCB->fcbFlNm == cnid) {
			return fBsyErr;
		}
	}

	int err = MF.Del(FID1, getDBName(cnid), isdir(cnid));
	if (err == EEXIST || err == ENOTEMPTY) return fBsyErr;
	else if (err) return ioErr;

	return noErr;
}

// Unlike Unix rename, this is not permitted to overwrite an existing file
static OSErr fsRename(struct IOParam *pb) {
	enum {CHILD=FID1, PARENT=FID2, JUNK=FID3};
	int32_t parentCNID, childCNID;

	// The original file exists
	childCNID = browse(CHILD, pbDirID(pb), pb->ioNamePtr);
	if (iserr(childCNID)) return childCNID;
	parentCNID = getDBParent(childCNID);

	char oldNameU[512], newNameU[512];
	unsigned char newNameR[256];

	// The old name is already in Unicode, and correct thanks to browse()
	strcpy(oldNameU, getDBName(childCNID));

	// The new name requires conversion
	pathSplitLeaf(pb->ioMisc, NULL, newNameR); // remove extraneous colons
	if (newNameR[0] > 31 || newNameR[0] < 1) return bdNamErr;
	utf8name(newNameU, newNameR);

	// Special case: rename the disk
	if (childCNID == 2) {
		if (newNameR[0] > 27) return bdNamErr;
		pstrcpy(vcb.vcbVN, newNameR);
		setDB(2, 1, newNameU);
		return noErr;
	}

	// Disallow a duplicate-looking filename
	// (The Unix/9P behaviour is to overwrite the destination silently)
	if (!iserr(browse(JUNK, parentCNID, newNameR))) return dupFNErr;

	// Need a PARENT for the Trenameat call
	Walk9(CHILD, PARENT, 1, (const char *[]){".."}, NULL, NULL);

	int lerr = MF.Move(PARENT, oldNameU, PARENT, newNameU);
	if (lerr) return ioErr;

	// Commit to the rename, so correct the database
	setDB(childCNID, parentCNID, newNameU);

	return noErr;
}

// -->    12    ioCompletion  pointer
// <--    16    ioResult      word
// -->    18    ioNamePtr     pointer
// -->    22    ioVRefNum     word
// -->    28    ioNewName     pointer
// -->    36    ioNewDirID    long word
// -->    48    ioDirID       long word
// Must not overwrite an existing file
static OSErr fsCatMove(struct CMovePBRec *pb) {
	// Move the file/directory with cnid1...
	int32_t cnid1 = browse(FID1, pbDirID(pb), pb->ioNamePtr);
	if (iserr(cnid1)) return cnid1;
	if (cnid1 == 2) return bdNamErr; // can't move root
	const char *name = getDBName(cnid1);

	// ...into the directory with cnid2.
	// Subtle bug: if the ioNewName is an absolute path to a *different disk*,
	// browse will nontheless carry on searching for the subpath in *this disk*.
	int32_t cnid2 = browse(FID2, pb->ioNewDirID, pb->ioNewName);
	if (iserr(cnid2)) return cnid2;
	if (!isdir(cnid2)) return bdNamErr;

	// Disallow a duplicate-looking filename
	// (The Unix/9P behaviour is to overwrite the destination silently)
	unsigned char romanname[32];
	mr31name(romanname, name);
	if (!iserr(browse(FID3, cnid2, romanname))) return dupFNErr; // FID3 is junk

	// Navigate "up" a level because 9P expects the parent fid
	Walk9(FID1, FID1, 1, (const char *[]){".."}, NULL, NULL);

	int lerr = MF.Move(FID1, getDBName(cnid1), FID2, getDBName(cnid1));
	if (lerr == EINVAL) badMovErr;
	else if (lerr) return ioErr;

	return noErr;
}

// "Working directories" are a compatibility shim for apps expecting flat disks:
// a table of fake volume reference numbers that actually refer to directories.
static OSErr fsOpenWD(struct WDParam *pb) {
	int32_t cnid = pbDirID(pb);
	cnid = browse(FID1, cnid, pb->ioNamePtr);
	if (iserr(cnid)) return cnid;
	if (!isdir(cnid)) return fnfErr;

	// The root: no need to create a WD, just return the volume's refnum
	if (cnid == 2) {
		pb->ioVRefNum = vcb.vcbVRefNum;
		return noErr;
	}

	// A copy of the desired WDCB (a straight comparison is okay)
	struct WDCBRec wdcb = {
		.wdVCBPtr = &vcb,
		.wdDirID = cnid,
		.wdProcID = pb->ioWDProcID,
	};

	short tablesize = *(short *)unaligned32(0x372); // int at start of table
	enum {SKIPWD = 2 + 2 * sizeof (struct WDCBRec)}; // never use 1st/2nd WDCB

	// Search for already-open WDCB
	for (short ref=WDLO+SKIPWD; ref<WDLO+tablesize; ref+=16) {
		if (!memcmp(findWD(ref), &wdcb, sizeof wdcb)) {
			pb->ioVRefNum = ref;
			return noErr;
		}
	}

	// Search for free WDCB
	for (short ref=WDLO+SKIPWD; ref<WDLO+tablesize; ref+=16) {
		if (findWD(ref)->wdVCBPtr == NULL) {
			memcpy(findWD(ref), &wdcb, sizeof wdcb);
			pb->ioVRefNum = ref;
			return noErr;
		}
	}

	return tmwdoErr;
}

static OSErr fsCloseWD(struct WDParam *pb) {
	struct WDCBRec *rec = findWD(pb->ioVRefNum);
	if (rec) memset(rec, 0, sizeof *rec);
	return noErr;
}

// Shabby little hack that might turn into something good
// flags == 0 means don't open, just navigate
int OpenSidecar(uint32_t fid, int32_t cnid, int flags, const char *fmt) {
	int err;

	err = browse(fid, cnid, "");
	if (err < 0) return ENOENT;

	Walk9(fid, fid, 1, (const char *[]){".."}, NULL, NULL);

	char sidename[1024]; // like file.rdump or ._file
	sprintf(sidename, fmt, getDBName(cnid));

	// Squalid raciness
	err = Walk9(fid, fid, 1, (const char *[]){sidename}, NULL, NULL);
	if (flags == 0) return err;

	if (err) goto trycreate;

	err = Lopen9(fid, flags, NULL, NULL);
	return err;

trycreate:
	// Should we be doing a dance to avoid a create-open race?
	err = Lcreate9(fid, flags, 0666, 0, sidename, NULL, NULL);
	return err;
}

// Divine the meaning of ioVRefNum and ioDirID
static int32_t pbDirID(void *_pb) {
	struct HFileParam *pb = _pb;

	// HFSDispatch or another hierarchical call: use dirID if nonzero
	if ((pb->ioTrap & 0xff) == 0x60 || (pb->ioTrap & 0x200) != 0) {
		if (pb->ioDirID != 0) {
			return pb->ioDirID;
		}
	}

	// Is it a WDCB?
	if (pb->ioVRefNum <= WDHI || pb->ioVRefNum == 0) {
		struct WDCBRec *wdcb = findWD(pb->ioVRefNum);
		if (wdcb) return wdcb->wdDirID;
	}

	// It's just the root
	return 2;
}

// Validate WDCB refnum and return the structure.
// A refnum of zero refers to the "current directory" WDCB
// Sadly the blocks are *always* non-4-byte-aligned
static struct WDCBRec *findWD(short refnum) {
	void *table = (void *)unaligned32(0x372);

	int16_t tblSize = *(int16_t *)table;
	int16_t offset = refnum ? refnum-WDLO : 2;

	if (offset>=2 && offset<tblSize && (offset%16)==2) {
		return table+offset;
	} else {
		return NULL;
	}
}

// Create a CNID from a 9P QID (which is a slightly adorned inode number)
// Negative CNIDs are reserved for errors, and the 2nd MSB means "not a dir"
// Warning: the "type" field of a Rreaddir QID is nonsense, causing this
// function to give a garbage result, so qidTypeFix() it before calling me.
static int32_t qid2cnid(struct Qid9 qid) {
	if (qid.path == root.path) return 2;

	int32_t cnid = 0;
	cnid ^= (0x3fffffffULL & qid.path);
	cnid ^= ((0x0fffffffc0000000ULL & qid.path) >> 30);
	cnid ^= ((0xf000000000000000ULL & qid.path) >> 40); // don't forget the upper 4 bits
	if (cnid < 16) cnid += 0x12342454; // low numbers reserved for system

	if ((qid.type & 0x80) == 0) cnid |= 0x40000000;

	return cnid;
}

static struct Qid9 qidTypeFix(struct Qid9 qid, char linuxType) {
	if (linuxType == 4) {
		qid.type = 0x80;
	} else {
		qid.type = 0;
	}
	return qid;
}

static bool iserr(int32_t cnid) {
	return cnid < 0;
}

static bool isdir(int32_t cnid) {
	return (cnid & 0x40000000) == 0;
}

// Print a CNID to the log as a /unix/path
static void cnidPrint(int32_t cnid) {
	if (!logenable) return;

	char big[512];
	int remain = sizeof big;

	while (cnid != 2) {
		const char *name = getDBName(cnid);
		int nsize = strlen(name);
		if (remain < nsize+1) break;

		remain -= nsize;
		memcpy(big + remain, name, nsize);
		big[--remain] = '/';

		cnid = getDBParent(cnid);
	}

	if (remain == sizeof big) big[--remain] = '/'; // root is not "empty path"

	printf("%.*s", sizeof big - remain, big + remain);
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

static struct VCB *findVol(short num) {
	for (struct VCB *i=(struct VCB *)GetVCBQHdr()->qHead;
		i!=NULL;
		i=(struct VCB *)i->qLink
	) {
		if (i->vcbVRefNum == num) return i;
	}
	return NULL;
}

static void pathSplitLeaf(const unsigned char *path, unsigned char *dir, unsigned char *name) {
	int dirlen = path[0], namelen = 0;

	if (path[dirlen] == ':') dirlen--;

	while (dirlen && path[dirlen] != ':') {
		dirlen--;
		namelen++;
	}

	if (dir) {
		dir[0] = dirlen;
		memcpy(dir+1, path+1, dirlen);
	}

	if (name) {
		name[0] = namelen;
		memcpy(name+1, path+1+dirlen, namelen);
	}
}

static bool visName(const char *name) {
	return (name[0] != '.' && !MF.IsSidecar(name));
}

static void startDB(void) {
	int sqerr;

	// Big memory buffer for "MEMSYS5", instead of using malloc etc
	static uint32_t dbbuf[196*1024/4];
	sqerr = sqlite3_config(SQLITE_CONFIG_HEAP, dbbuf, sizeof dbbuf, /*mnReq important*/ 16);
	if (sqerr != SQLITE_OK) panic("sqlite3_config");

	sqerr = sqlite3_initialize();
	if (sqerr != SQLITE_OK) panic("sqlite3_initialize");

	sqerr = sqlite3_open("deebee", &metadb);
	if (sqerr != SQLITE_OK) panic("sqlite3_open");

	char *msg;
	sqerr = sqlite3_exec(metadb, "CREATE TABLE IF NOT EXISTS catalog (id INTEGER PRIMARY KEY, parentid INTEGER, name BLOB);", NULL, NULL, &msg);
	printf("err=%s\n", msg);
	if (sqerr != SQLITE_OK) panic("sqlite3_exec");
}

static void setDB(int32_t cnid, int32_t pcnid, const char *name) {
	sqlite3_stmt *S = PERSISTENT_STMT(metadb, "INSERT OR REPLACE INTO catalog (id, parentid, name) VALUES (?, ?, ?);");

	if (sqlite3_bind_int(S, 1, cnid)) panic("bind1");
	if (sqlite3_bind_int(S, 2, pcnid)) panic("bind2");
	if (sqlite3_bind_text(S, 3, name, -1, NULL)) panic("bind3");

	if (sqlite3_step(S) != SQLITE_DONE) panic("step");

	sqlite3_reset(S);
	sqlite3_clear_bindings(S);
}

// NULL on failure (bad CNID)
static const char *getDBName(int32_t cnid) {
	static char ret[512];

	sqlite3_stmt *S = PERSISTENT_STMT(metadb, "SELECT (name) FROM catalog WHERE id == ?;");

	if (sqlite3_bind_int(S, 1, cnid)) panic("bind1");

	if (sqlite3_step(S) == SQLITE_ROW) {
		strcpy(ret, sqlite3_column_text(S, 0));
	} else {
		ret[0] = 0;
	}

	sqlite3_reset(S);
	sqlite3_clear_bindings(S);

	return ret;
}

// Zero on failure (bad CNID)
static int32_t getDBParent(int32_t cnid) {
	int32_t ret;

	sqlite3_stmt *S = PERSISTENT_STMT(metadb, "SELECT (parentid) FROM catalog WHERE id == ?;");

	if (sqlite3_bind_int(S, 1, cnid)) panic("bind1");

	if (sqlite3_step(S) == SQLITE_ROW) {
		ret = sqlite3_column_int(S, 0);
	} else {
		ret = 0;
	}

	sqlite3_reset(S);
	sqlite3_clear_bindings(S);

	return ret;
}

int32_t mactime(int64_t unixtime) {
	struct MachineLocation loc;
	ReadLocation(&loc);
	int32_t tz = loc.u.gmtDelta & 0xffffff;
	if (tz & 0x800000) tz -= 0x1000000; // sign-extend

	// Mac epoch = 1904, Unix epoch = 1970 (24107 days)
	// Mac time is TZ local
	uint64_t mactime = unixtime + (24107)*24*60*60 + tz;

	// The time "epoch+0" (start of 1904) has special meaning to MPW ("file corrupt")
	// Let's recast this as everything before MacE+0x80000000, which is only 2 years after the Unix epoch
	if (mactime < 0x80000000UL) return 0;

	// What to do about the 2040 time rollover problem?
	if (mactime > 0xffffffffUL) return 0xffffffff;

	return mactime;
}

static long fsCall(void *pb, long selector) {
	static unsigned char hdr;
	if (hdr++ == 0) {
		printf("%lu%% (browse/total) %d%% (relist/total)\n", browseTimer*100/(hfsTimer+1), relistTimer*100/(hfsTimer+1));
	}

	TIMEFUNC(hfsTimer);

	// Hideously nasty stack-sniffing debug code
// 	void *top = LMGetMemTop() - 32;
// 	if (top > stack+512) top = stack+512;
// 	while (stack < top) {
// 		void *addr = *(void **)stack - 2;
// 		void *memtop = LMGetMemTop();
// 		void *romstart = LMGetROMBase();
// 		void *romend = romstart + *(uint32_t *)(romstart + 64);
//
// 		if (((int)addr & 1) == 0 && (addr < memtop || (addr >= romstart && addr < romend))) {
// 			if (*(uint16_t *)addr == *(uint16_t *)(pb + 6)) {
// 				printf("caller seems to be %p\n", addr);
// 				break;
// 			}
// 		}
//
// 		stack += 2;
// 	}

	unsigned short trap = ((struct IOParam *)pb)->ioTrap;

	// Use the selector format of the File System Manager
	if ((trap & 0xff) == 0x60) { // HFSDispatch
		selector = (selector & 0xff) | (trap & 0xf00);
	} else {
		selector = trap;
	}

	if (logenable) {
		printf("FS_%s", PBPrint(pb, selector, 1));
		strcat(logprefix, "     ");
	}

	OSErr result = fsDispatch(pb, selector);

	if (logenable) {
		logprefix[strlen(logprefix) - 5] = 0;
		printf("%s", PBPrint(pb, selector, result));
	}

	return result;
}

// This makes it easy to have a selector return noErr without a function
static OSErr fsDispatch(void *pb, unsigned short selector) {
	switch (selector & 0xf0ff) {
	case kFSMOpen: return fsOpen(pb);
	case kFSMClose: return fsClose(pb);
	case kFSMRead: return fsRead(pb);
	case kFSMWrite: return fsWrite(pb);
	case kFSMGetVolInfo: return fsGetVolInfo(pb);
	case kFSMCreate: return fsCreate(pb);
	case kFSMDelete: return fsDelete(pb);
	case kFSMOpenRF: return fsOpen(pb);
	case kFSMRename: return fsRename(pb);
	case kFSMGetFileInfo: return fsGetFileInfo(pb);
	case kFSMSetFileInfo: return fsSetFileInfo(pb);
	case kFSMUnmountVol: return extFSErr;
	case kFSMMountVol: return fsMountVol(pb);
	case kFSMAllocate: return noErr;
	case kFSMGetEOF: return fsGetEOF(pb);
	case kFSMSetEOF: return fsSetEOF(pb);
	case kFSMFlushVol: return noErr;
	case kFSMGetVol: return extFSErr; // FM handles
	case kFSMSetVol: return fsSetVol(pb);
	case kFSMEject: return extFSErr;
	case kFSMGetFPos: return fsRead(pb);
	case kFSMOffline: return extFSErr;
	case kFSMSetFilLock: return extFSErr;
	case kFSMRstFilLock: return extFSErr;
	case kFSMSetFilType: return extFSErr;
	case kFSMSetFPos: return fsRead(pb);
	case kFSMFlushFile: return noErr;
	case kFSMOpenWD: return fsOpenWD(pb);
	case kFSMCloseWD: return fsCloseWD(pb);
	case kFSMCatMove: return fsCatMove(pb);
	case kFSMDirCreate: return fsCreate(pb);
	case kFSMGetWDInfo: return noErr;
	case kFSMGetFCBInfo: return noErr;
	case kFSMGetCatInfo: return fsGetFileInfo(pb);
	case kFSMSetCatInfo: return fsSetFileInfo(pb);
	case kFSMSetVolInfo: return noErr;
	case kFSMLockRng: return extFSErr;
	case kFSMUnlockRng: return extFSErr;
	case kFSMXGetVolInfo: return fsGetVolInfo(pb);
	case kFSMCreateFileIDRef: return extFSErr;
	case kFSMDeleteFileIDRef: return extFSErr;
	case kFSMResolveFileIDRef: return extFSErr;
	case kFSMExchangeFiles: return extFSErr;
	case kFSMCatSearch: return extFSErr;
	case kFSMOpenDF: return fsOpen(pb);
	case kFSMMakeFSSpec: return fsMakeFSSpec(pb);
	case kFSMDTGetPath: return extFSErr;
	case kFSMDTCloseDown: return extFSErr;
	case kFSMDTAddIcon: return extFSErr;
	case kFSMDTGetIcon: return extFSErr;
	case kFSMDTGetIconInfo: return extFSErr;
	case kFSMDTAddAPPL: return extFSErr;
	case kFSMDTRemoveAPPL: return extFSErr;
	case kFSMDTGetAPPL: return extFSErr;
	case kFSMDTSetComment: return extFSErr;
	case kFSMDTRemoveComment: return extFSErr;
	case kFSMDTGetComment: return extFSErr;
	case kFSMDTFlush: return extFSErr;
	case kFSMDTReset: return extFSErr;
	case kFSMDTGetInfo: return extFSErr;
	case kFSMDTOpenInform: return extFSErr;
	case kFSMDTDelete: return extFSErr;
	case kFSMGetVolParms: return fsGetVolParms(pb);
	case kFSMGetLogInInfo: return extFSErr;
	case kFSMGetDirAccess: return extFSErr;
	case kFSMSetDirAccess: return extFSErr;
	case kFSMMapID: return extFSErr;
	case kFSMMapName: return extFSErr;
	case kFSMCopyFile: return extFSErr;
	case kFSMMoveRename: return extFSErr;
	case kFSMOpenDeny: return extFSErr;
	case kFSMOpenRFDeny: return extFSErr;
	case kFSMGetXCatInfo: return extFSErr;
	case kFSMGetVolMountInfoSize: return extFSErr;
	case kFSMGetVolMountInfo: return extFSErr;
	case kFSMVolumeMount: return extFSErr;
	case kFSMShare: return extFSErr;
	case kFSMUnShare: return extFSErr;
	case kFSMGetUGEntry: return extFSErr;
	case kFSMGetForeignPrivs: return extFSErr;
	case kFSMSetForeignPrivs: return extFSErr;
	case kFSMGetVolumeInfo: return extFSErr;
	case kFSMSetVolumeInfo: return extFSErr;
	case kFSMReadFork: return extFSErr;
	case kFSMWriteFork: return extFSErr;
	case kFSMGetForkPosition: return extFSErr;
	case kFSMSetForkPosition: return extFSErr;
	case kFSMGetForkSize: return extFSErr;
	case kFSMSetForkSize: return extFSErr;
	case kFSMAllocateFork: return extFSErr;
	case kFSMFlushFork: return noErr;
	case kFSMCloseFork: return extFSErr;
	case kFSMGetForkCBInfo: return extFSErr;
	case kFSMCloseIterator: return extFSErr;
	case kFSMGetCatalogInfoBulk: return extFSErr;
	case kFSMCatalogSearch: return extFSErr;
	case kFSMMakeFSRef: return extFSErr;
	case kFSMCreateFileUnicode: return extFSErr;
	case kFSMCreateDirUnicode: return extFSErr;
	case kFSMDeleteObject: return extFSErr;
	case kFSMMoveObject: return extFSErr;
	case kFSMRenameUnicode: return extFSErr;
	case kFSMExchangeObjects: return extFSErr;
	case kFSMGetCatalogInfo: return extFSErr;
	case kFSMSetCatalogInfo: return extFSErr;
	case kFSMOpenIterator: return extFSErr;
	case kFSMOpenFork: return extFSErr;
	case kFSMMakeFSRefUnicode: return extFSErr;
	case kFSMCompareFSRefs: return extFSErr;
	case kFSMCreateFork: return extFSErr;
	case kFSMDeleteFork: return extFSErr;
	case kFSMIterateForks: return extFSErr;
	default: return extFSErr;
	}
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
		// dynamically create the "location" field, goes in the Get Info window
	};

	hd.location[0] = sprintf(hd.location + 1, "Virtio 9P device (%s)", MF.Name);

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
	switch (selector) {
	case kDriveIcon: return cIcon(pb);
	case kMediaIcon: return cIcon(pb);
	case kDriveInfo: return cDriveInfo(pb);
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
