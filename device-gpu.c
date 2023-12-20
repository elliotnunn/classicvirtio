/* Copyright (c) 2023 Elliot Nunn */
/* Licensed under the MIT license */

#include <stdio.h>
#include <Devices.h>
#include <Displays.h>
#include <DriverServices.h>
#include <Events.h>
#include <fp.h>
#include <Gestalt.h>
#include <LowMem.h>
#include <MixedMode.h>
#include <NameRegistry.h>
#include <string.h>
#include <Traps.h>
#include <Video.h>
#include <VideoServices.h>

#include "allocator.h"
#include "atomic.h"
#include "blit.h"
#include "callupp.h"
#include "dirtyrectpatch.h"
#include "gammatables.h"
#include "printf.h"
#include "panic.h"
#include "patch68k.h"
#include "transport.h"
#include "structs-gpu.h"
#include "virtqueue.h"

#include <stdbool.h>

#include "device.h"


// The classics
#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

enum {
	MAXBUF = 64*1024*1024, // enough for 4096x4096
	MINBUF = 2*1024*1024, // enough for 800x600
	FAST_REFRESH = -16626, // before QD callbacks work, microsec, 60.15 Hz
	SLOW_REFRESH = 1000, // after QD callbacks work, millisec, 1 Hz
	CURSOREDGE = 16,
};

enum {
	k1bit = kDepthMode1,
	k2bit = kDepthMode2,
	k4bit = kDepthMode3,
	k8bit = kDepthMode4,
	k16bit = kDepthMode5,
	k32bit = kDepthMode6,
	kDepthModeMax = kDepthMode6
};

OSStatus DoDriverIO(AddressSpaceID spaceID, IOCommandID cmdID,
	IOCommandContents pb, IOCommandCode code, IOCommandKind kind);
static OSStatus initialize(DriverInitInfo *info);
static OSStatus finalize(DriverFinalInfo *info);
static OSStatus control(short csCode, void *param);
static OSStatus status(short csCode, void *param);
static void transact(void *req, size_t req_size, void *reply, size_t reply_size);
static void getSuggestedSizes(struct virtio_gpu_display_one pmodes[16]);
static void getBestSize(short *width, short *height);
static uint32_t idForRes(short width, short height, bool force);
static uint32_t resCount(void);
static bool mode(int new_depth, uint32_t new_rez);
static uint32_t setVirtioScanout(int idx, short rowbytes, short w, short h, uint32_t *page_list);
static void notificationProc(NMRecPtr nmReqPtr);
static void notificationAtomic(NMRecPtr nmReqPtr);
static void debugPoll(void);
static void lateBootHook(void);
static void updateScreen(short t, short l, short b, short r);
static void sendPixels(uint32_t topleft, uint32_t botright);
static void perfTest(void);
static OSStatus VBL(void *p1, void *p2);
static long rowbytesForBack(int relativeDepth, long width);
static long rowbytesForFront(int relativeDepth, long width);
static void reCLUT(int index);
static void grayPattern(void);
static void grayCLUT(void);
static void linearCLUT(void);
static void setGammaTable(GammaTbl *tbl);
static OSStatus GetBaseAddr(VDPageInfo *rec);
static OSStatus MySetEntries(VDSetEntryRecord *rec);
static OSStatus DirectSetEntries(VDSetEntryRecord *rec);
static OSStatus GetEntries(VDSetEntryRecord *rec);
static OSStatus GetClutBehavior(VDClutBehavior *rec);
static OSStatus SetClutBehavior(VDClutBehavior *rec);
static OSStatus SetGamma(VDGammaRecord *rec);
static OSStatus GetGamma(VDGammaRecord *rec);
static OSStatus GetGammaInfoList(VDGetGammaListRec *rec);
static OSStatus RetrieveGammaTable(VDRetrieveGammaRec *rec);
static OSStatus GrayPage(VDPageInfo *rec);
static OSStatus SetGray(VDGrayRecord *rec);
static OSStatus GetGray(VDGrayRecord *rec);
static OSStatus GetPages(VDPageInfo *rec);
static OSStatus SetInterrupt(VDFlagRecord *rec);
static OSStatus GetInterrupt(VDFlagRecord *rec);
static OSStatus SetSync(VDSyncInfoRec *rec);
static OSStatus GetSync(VDSyncInfoRec *rec);
static OSStatus SetPowerState(VDPowerStateRec *rec);
static OSStatus GetPowerState(VDPowerStateRec *rec);
static OSStatus SavePreferredConfiguration(VDSwitchInfoRec *rec);
static OSStatus GetPreferredConfiguration(VDSwitchInfoRec *rec);
static OSStatus GetConnection(VDDisplayConnectInfoRec *rec);
static OSStatus GetMode(VDPageInfo *rec);
static OSStatus GetCurMode(VDSwitchInfoRec *rec);
static OSStatus GetModeTiming(VDTimingInfoRec *rec);
static OSStatus SetMode(VDPageInfo *rec);
static OSStatus SwitchMode(VDSwitchInfoRec *rec);
static OSStatus GetNextResolution(VDResolutionInfoRec *rec);
static OSStatus GetVideoParameters(VDVideoParametersInfoRec *rec);
static OSStatus SupportsHardwareCursor(VDSupportsHardwareCursorRec *rec);
static OSStatus GetHardwareCursorDrawState(VDHardwareCursorDrawStateRec *rec);
static OSStatus SetHardwareCursor(VDSetHardwareCursorRec *rec);
static OSStatus DrawHardwareCursor(VDDrawHardwareCursorRec *rec);
static void gammaCursor(void);
static void blitCursor(void);

// Allocate one 4096-byte page for all our screen-update buffers.
// (16 is the maximum number of 192-byte chunks fitting in a page)
static void *lpage;
static uint32_t ppage;
static int maxinflight = 16;
static uint16_t freebufs;

// Allocate two large framebuffers
static void *backbuf, *frontbuf;
static size_t bufsize;
static uint32_t fbpages[MAXBUF/4096];
static uint32_t screen_resource = 100;

// init routine polls this after sending a buffer
static volatile void *last_tag;

// Current dimensions, depth and color settings
struct rez {short w; short h;};
struct rez rezzes[] = {
	{512, 342},
	{640, 480},
	{800, 600},
	{1024, 768},
	{0, 0},
};
static short W, H, rowbytes_back, rowbytes_front;
static int depth;
static ColorSpec public_clut[256];
static uint32_t private_clut[256];

static uint8_t gamma_red[256];
static uint8_t gamma_grn[256];
static uint8_t gamma_blu[256];
static char gamma_public[1024];

// Fake vertical blanking interrupts
static InterruptServiceIDType vblservice;
static AbsoluteTime vbltime;
static TimerID vbltimer;
static bool vblon = true;
static bool qdworks;

static bool pending_notification; // deduplicate NMInstall
static bool change_in_progress; // SetMode/SwitchMode lock out frame interrupts

// Cursor that the driver composites (like a hardware cursor)
static bool curs_set;
static bool curs_visible;
static bool curs_inverts;
static short curs_t, curs_l, curs_b, curs_r;
static uint32_t curs_back[CURSOREDGE*CURSOREDGE];
static uint32_t curs_front[CURSOREDGE*CURSOREDGE];

DriverDescription TheDriverDescription = {
	kTheDescriptionSignature,
	kInitialDriverDescriptor,
	{"\x0cpci1af4,1050", {0x00, 0x10, 0x80, 0x00}}, // v0.1
	{kDriverIsUnderExpertControl |
		kDriverIsOpenedUponLoad,
		"\x1b.Display_Video_Apple_Virtio"}, // "Apple" = interface not vendor
	{1, // nServices
	{{kServiceCategoryNdrvDriver, kNdrvTypeIsVideo, {0x00, 0x10, 0x80, 0x00}}}} //v0.1
};

const unsigned short drvrFlags = dNeedLockMask|dStatEnableMask|dCtlEnableMask;
const char drvrNameVers[] = "\x1b.Display_Video_Apple_Virtio\x01\x00";

static const char *controlNames[] = {
	"Reset",                        // 0
	"KillIO",                       // 1
	"SetMode",                      // 2
	"SetEntries",                   // 3
	"SetGamma",                     // 4
	"GrayScreen",                   // 5
	"SetGray",                      // 6
	"SetInterrupt",                 // 7
	"DirectSetEntries",             // 8
	"SetDefaultMode",               // 9
	"SwitchMode",                   // 10
	"SetSync",                      // 11
	NULL,                           // 12
	NULL,                           // 13
	NULL,                           // 14
	NULL,                           // 15
	"SavePreferredConfiguration",   // 16
	NULL,                           // 17
	NULL,                           // 18
	NULL,                           // 19
	NULL,                           // 20
	NULL,                           // 21
	"SetHardwareCursor",            // 22
	"DrawHardwareCursor",           // 23
	"SetConvolution",               // 24
	"SetPowerState",                // 25
	"PrivateControlCall",           // 26
	NULL,                           // 27
	"SetMultiConnect",              // 28
	"SetClutBehavior",              // 29
	NULL,                           // 30
	"SetDetailedTiming",            // 31
	NULL,                           // 32
	"DoCommunication",              // 33
	"ProbeConnection",              // 34
};

static const char *statusNames[] = {
	NULL,                           // 0
	NULL,                           // 1
	"GetMode",                      // 2
	"GetEntries",                   // 3
	"GetPages",                     // 4
	"GetBaseAddr",                  // 5
	"GetGray",                      // 6
	"GetInterrupt",                 // 7
	"GetGamma",                     // 8
	"GetDefaultMode",               // 9
	"GetCurMode",                   // 10
	"GetSync",                      // 11
	"GetConnection",                // 12
	"GetModeTiming",                // 13
	"GetModeBaseAddress",           // 14
	"GetScanProc",                  // 15
	"GetPreferredConfiguration",    // 16
	"GetNextResolution",            // 17
	"GetVideoParameters",           // 18
	NULL,                           // 19
	"GetGammaInfoList",             // 20
	"RetrieveGammaTable",           // 21
	"SupportsHardwareCursor",       // 22
	"GetHardwareCursorDrawState",   // 23
	"GetConvolution",               // 24
	"GetPowerState",                // 25
	"PrivateStatusCall",            // 26
	"GetDDCBlock",                  // 27
	"GetMultiConnect",              // 28
	"GetClutBehavior",              // 29
	"GetTimingRanges",              // 30
	"GetDetailedTiming",            // 31
	"GetCommunicationInfo",         // 32
};

extern OSStatus DoDriverIO(AddressSpaceID spaceID, IOCommandID cmdID,
	IOCommandContents pb, IOCommandCode code, IOCommandKind kind) {
	OSStatus err;

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
		if (logenable && (*pb.pb).cntrlParam.csCode != cscDrawHardwareCursor) {
			if ((*pb.pb).cntrlParam.csCode < sizeof(controlNames)/sizeof(*controlNames)) {
				printf("Control(%s)\n", controlNames[(*pb.pb).cntrlParam.csCode]);
			} else {
				printf("Control(%d)\n", (*pb.pb).cntrlParam.csCode);
			}
		}

		err = control((*pb.pb).cntrlParam.csCode, *(void **)&(*pb.pb).cntrlParam.csParam);

		if (logenable && (*pb.pb).cntrlParam.csCode != cscDrawHardwareCursor) {
			printf("    = %d\n", err);
		}

		break;
	case kStatusCommand:
		if (logenable) {
			if ((*pb.pb).cntrlParam.csCode < sizeof(statusNames)/sizeof(*statusNames)) {
				printf("Status(%s)\n", statusNames[(*pb.pb).cntrlParam.csCode]);
			} else {
				printf("Status(%d)\n", (*pb.pb).cntrlParam.csCode);
			}
		}

		err = status((*pb.pb).cntrlParam.csCode, *(void **)&(*pb.pb).cntrlParam.csParam);

		if (logenable) {
			printf("    = %d\n", err);
		}

		break;
	case kOpenCommand:
	case kCloseCommand:
		err = noErr;
		break;
	default:
		err = paramErr;
		break;
	}

	// Return directly from every call
	if (kind & kImmediateIOCommandKind) {
		return err;
	} else {
		return IOCommandIsComplete(cmdID, err);
	}
}

static OSStatus initialize(DriverInitInfo *info) {
	long ram = 0;
	short width, height;

	sprintf(logprefix, "%.*s(%d) ", *drvrNameVers, drvrNameVers+1, info->refNum);
// 	if (0 == RegistryPropertyGet(&info->deviceEntry, "debug", NULL, 0)) {
		logenable = 1;
// 	}

	printf("Starting\n");

	// No need to signal FAILED if cannot communicate with device
	if (!VInit(&info->deviceEntry)) {
		printf("Transport layer failure\n");
		return openErr;
	};

	if (!VFeaturesOK()) {
		printf("Feature negotiation failure\n");
		goto fail;
	}

	// Can have (descriptor count)/4 updateScreens in flight at once
	maxinflight = QInit(0, 4*maxinflight /*n(descriptors)*/) / 4;
	if (maxinflight < 1) {
		printf("Virtqueue layer failure\n");
		goto fail;
	}

	freebufs = (1 << maxinflight) - 1;

	// All our descriptors point into this wired-down page
	lpage = AllocPages(1, &ppage);
	if (lpage == NULL) {
		printf("Memory allocation failure\n");
		goto fail;
	}

	// Use no more than a quarter of RAM (but allow for a few KB short of a MB)
	bufsize = MAXBUF;
	Gestalt('ram ', &ram);
	while (ram != 0 && bufsize > (ram+0x10000)/8) bufsize /= 2;

	// Allocate the largest two framebuffers possible
	for (;;) {
		backbuf = PoolAllocateResident(bufsize, true);
		frontbuf = AllocPages(bufsize/4096, fbpages);

		if (backbuf != NULL && frontbuf != NULL) break;

		if (backbuf != NULL) PoolDeallocate(backbuf);
		if (frontbuf != NULL) FreePages(frontbuf);

		bufsize /= 2;
		if (bufsize < MINBUF) {
			printf("Framebuffer allocation failure\n");
			goto fail;
		}
	}

	// Cannot go any further without touching virtqueues, which requires DRIVER_OK
	VDriverOK();

	getBestSize(&width, &height);
	if (!mode(k32bit, idForRes(width, height, true))) {
		panic("Could not start up in any mode");
	}

	setGammaTable((GammaTbl *)&builtinGamma[0].table);
	linearCLUT();
	grayPattern();
	updateScreen(0, 0, H, W);

	// Initially VBL interrupts must be fast
	VSLNewInterruptService(&info->deviceEntry, kVBLInterruptServiceType, &vblservice);
	vbltime = AddDurationToAbsolute(FAST_REFRESH, UpTime());
	SetInterruptTimer(&vbltime, VBL, NULL, &vbltimer);

	// MacsBug disables interrupts (including our frame timer)
	// so catch when it polls the keyboard (continuously)
	printf("Hooking DebugUtil for screen updates: ");
	Patch68k(
		_DebugUtil,
		"0c80 00000003" //      cmp.l   #3,d0
		"660e"          //      bne.s   old
		"48e7 e0e0"     //      movem.l d0-d2/a0-a2,-(sp)
		"4eb9 %l"       //      jsr     debugPoll
		"4cdf 0707"     //      movem.l (sp)+,d0-d2/a0-a2
		"4ef9 %o",      // old: jmp     originalDebugUtil
		STATICDESCRIPTOR(debugPoll, kCStackBased)
	);

	// We can only patch drawing code *after* QuickDraw is fully installed,
	// so wait for ProcessMgr to NewGestalt('os  ') right before the desktop.
	printf("Hooking Process Manager startup: ");
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
		STATICDESCRIPTOR(lateBootHook, kCStackBased)
	);

	return noErr;

fail:
	if (lpage) FreePages(lpage);
	if (backbuf) PoolDeallocate(backbuf);
	if (frontbuf) FreePages(frontbuf);
	VFail();
	return openErr;
}

static OSStatus finalize(DriverFinalInfo *info) {
	return noErr;
}

static OSStatus control(short csCode, void *param) {
	switch (csCode) {
		case cscDirectSetEntries: return DirectSetEntries(param);
		case cscDrawHardwareCursor: return DrawHardwareCursor(param);
		case cscGrayPage: return GrayPage(param);
		case cscSavePreferredConfiguration: return SavePreferredConfiguration(param);
		case cscSetClutBehavior: return SetClutBehavior(param);
		case cscSetEntries: return MySetEntries(param);
		case cscSetGamma: return SetGamma(param);
		case cscSetGray: return SetGray(param);
		case cscSetHardwareCursor: return SetHardwareCursor(param);
		case cscSetInterrupt: return SetInterrupt(param);
		case cscSetMode: return SetMode(param);
		case cscSetPowerState: return SetPowerState(param);
		case cscSetSync: return SetSync(param);
		case cscSwitchMode: return SwitchMode(param);
	}
	return controlErr;
}

static OSStatus status(short csCode, void *param) {
	switch (csCode) {
		case cscGetBaseAddr: return GetBaseAddr(param);
		case cscGetClutBehavior: return GetClutBehavior(param);
		case cscGetConnection: return GetConnection(param);
		case cscGetCurMode: return GetCurMode(param);
		case cscGetEntries: return GetEntries(param);
		case cscGetGamma: return GetGamma(param);
		case cscGetGammaInfoList: return GetGammaInfoList(param);
		case cscGetGray: return GetGray(param);
		case cscGetHardwareCursorDrawState: return GetHardwareCursorDrawState(param);
		case cscGetInterrupt: return GetInterrupt(param);
		case cscGetMode: return GetMode(param);
		case cscGetModeTiming: return GetModeTiming(param);
		case cscGetNextResolution: return GetNextResolution(param);
		case cscGetPages: return GetPages(param);
		case cscGetPowerState: return GetPowerState(param);
		case cscGetPreferredConfiguration: return GetPreferredConfiguration(param);
		case cscGetSync: return GetSync(param);
		case cscGetVideoParameters: return GetVideoParameters(param);
		case cscRetrieveGammaTable: return RetrieveGammaTable(param);
		case cscSupportsHardwareCursor: return SupportsHardwareCursor(param);
	}
	return statusErr;
}

// Synchronous transaction instead of the usual async queue, must not interrupt
static void transact(void *req, size_t req_size, void *reply, size_t reply_size) {
	uint32_t physical_bufs[2], sizes[2];

	physical_bufs[0] = ppage;
	physical_bufs[1] = ppage + 2048;
	sizes[0] = req_size;
	sizes[1] = reply_size;

	while (freebufs != ((1 << maxinflight) - 1)) QPoll(0);

	memcpy(lpage, req, req_size);
	last_tag = (void *)'wait';
	QSend(0, 1, 1, physical_bufs, sizes, (void *)'done');
	QNotify(0);
	while (last_tag != (void *)'done') QPoll(0);
	memcpy(reply, (char *)lpage + 2048, reply_size);
}

static void getSuggestedSizes(struct virtio_gpu_display_one pmodes[16]) {
	struct virtio_gpu_ctrl_hdr request = {
		VIRTIO_GPU_CMD_GET_DISPLAY_INFO,
		VIRTIO_GPU_FLAG_FENCE};
	struct virtio_gpu_resp_display_info reply = {0};

	transact(&request, sizeof(request), &reply, sizeof(reply));
	memcpy(pmodes, reply.pmodes, sizeof(reply.pmodes));
}

static void getBestSize(short *width, short *height) {
	long w, h;
	int i;
	struct virtio_gpu_display_one pmodes[16];

	getSuggestedSizes(pmodes);

	for (i=0; i<16; i++) {
		if (pmodes[i].enabled) break;
	}

	if (i == 16) {
		*width = 800;
		*height = 600;
		return;
	}

	w = pmodes[i].r.width;
	h = pmodes[i].r.height;

	// Not enough RAM allocated? Try for smaller
	if (h * rowbytesForBack(k32bit, w) > bufsize) {
		// Cancel aspect ratio down to its simplest form
		long wr=w, hr=h;
		for (i=2; i<wr && i<hr; i++) {
			while (wr%i == 0 && hr%i == 0) {
				wr /= i;
				hr /= i;
			}
		}

		do {
			if (wr<=256 && hr<=256) {
				// Match aspect ratio precisely
				w -= wr;
				h -= hr;
			} else if (w > h) {
				// Approximate smaller widescreen
				h -= 1;
				w = (h*wr + hr/2) / hr; // round to nearest integer
			} else {
				// Approximate smaller tallscreen
				w -= 1;
				h = (w*hr + wr/2) / wr;
			}
		} while (h * rowbytesForBack(k32bit, w) > bufsize);
	}

	*width = w;
	*height = h;
}

static uint32_t idForRes(short width, short height, bool force) {
	int i;
	for (i=0; i<sizeof(rezzes)/sizeof(*rezzes)-1; i++) {
		if (rezzes[i].w == width && rezzes[i].h == height) break;
	}
	if (force) {
		rezzes[i].w = width;
		rezzes[i].h = height;
	}
	return i + 1;
}

static uint32_t resCount(void) {
	uint32_t n;
	n = sizeof(rezzes)/sizeof(*rezzes);
	if (rezzes[n-1].w == 0) n--;
	return n;
}

static bool mode(int new_depth, uint32_t new_rez) {
	uint32_t resource;
	short width, height;

	width = rezzes[new_rez-1].w;
	height = rezzes[new_rez-1].h;

	change_in_progress = true;
	resource = setVirtioScanout(0 /*scanout id*/, rowbytesForFront(new_depth, width), width, height, fbpages);
	if (!resource) {
		change_in_progress = false;
		return false;
	}
	screen_resource = resource;
	W = width;
	H = height;
	depth = new_depth;
	rowbytes_back = rowbytesForBack(depth, W);
	rowbytes_front = rowbytesForFront(depth, W);
	change_in_progress = false;

	return true;
}

// Must be called atomically
// Returns the resource ID, or zero if you prefer
static uint32_t setVirtioScanout(int idx, short rowbytes, short w, short h, uint32_t *page_list) {
	struct virtio_gpu_resource_detach_backing detach_backing = {0};
	struct virtio_gpu_resource_create_2d create_2d = {0};
	struct virtio_gpu_resource_attach_backing attach_backing = {0};
	struct virtio_gpu_set_scanout set_scanout = {0};
	struct virtio_gpu_resource_unref resource_unref = {0};
	struct virtio_gpu_ctrl_hdr reply;

	static uint32_t res_ids[16];
	uint32_t old_resource = res_ids[idx];
	uint32_t new_resource = res_ids[idx] ? (res_ids[idx] ^ 1) : (100 + 2*idx);

	// Divide page_list into contiguous segments, hopefully not too many
	size_t pgcnt = ((size_t)rowbytes * h + 0xfff) / 0x1000;
	int extents[126] = {0};
	int extcnt = 0;
	int i, j;

	for (i=0; i<pgcnt; i++) {
		if (i == 0 || page_list[i] != page_list[i-1]+0x1000) {
			// page_list too fragmented so fail gracefully
			if (extcnt == sizeof(extents)/sizeof(*extents)) return 0;

			extcnt++;
		}

		extents[extcnt-1]++;
	}

	// Create a host resource using VIRTIO_GPU_CMD_RESOURCE_CREATE_2D.
	create_2d.hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
	create_2d.hdr.flags = VIRTIO_GPU_FLAG_FENCE;
	create_2d.resource_id = new_resource;
	create_2d.format = VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM;
	create_2d.width = rowbytes/4;
	create_2d.height = h;

	transact(&create_2d, sizeof(create_2d), &reply, sizeof(reply));

	// Gracefully handle host running out of room for the resource
	if (reply.type != VIRTIO_GPU_RESP_OK_NODATA) return 0;

	// Detach backing from the old resource, but don't delete it yet.
	if (old_resource != 0) {
		detach_backing.hdr.type = VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING;
		detach_backing.hdr.flags = VIRTIO_GPU_FLAG_FENCE;
		detach_backing.resource_id = old_resource;

		transact(&detach_backing, sizeof(detach_backing), &reply, sizeof(reply));
	}

	// Attach guest allocated backing memory to the resource just created.
	attach_backing.hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
	attach_backing.hdr.flags = VIRTIO_GPU_FLAG_FENCE;
	attach_backing.resource_id = new_resource;
	attach_backing.nr_entries = extcnt;

	for (i=j=0; i<extcnt; j+=extents[i++]) {
		attach_backing.entries[i].addr = page_list[j];
		attach_backing.entries[i].length = extents[i] * 0x1000;
	}

	transact(&attach_backing, sizeof(attach_backing), &reply, sizeof(reply));

	// Use VIRTIO_GPU_CMD_SET_SCANOUT to link the framebuffer to a display scanout.
	set_scanout.hdr.type = VIRTIO_GPU_CMD_SET_SCANOUT;
	set_scanout.hdr.flags = VIRTIO_GPU_FLAG_FENCE;
	set_scanout.r.x = 0;
	set_scanout.r.y = 0;
	set_scanout.r.width = w;
	set_scanout.r.height = h;
	set_scanout.scanout_id = 0; // index, 0-15
	set_scanout.resource_id = new_resource;

	transact(&set_scanout, sizeof(set_scanout), &reply, sizeof(reply));

	if (old_resource != 0) {
		resource_unref.hdr.type = VIRTIO_GPU_CMD_RESOURCE_UNREF;
		resource_unref.hdr.flags = VIRTIO_GPU_FLAG_FENCE;
		resource_unref.resource_id = old_resource;

		transact(&resource_unref, sizeof(resource_unref), &reply, sizeof(reply));
	}

	res_ids[idx] = new_resource;
	return new_resource;
}

void DConfigChange(void) {
	// Post a notification to get some system task time
	if (!pending_notification) {
		static RoutineDescriptor descriptor = BUILD_ROUTINE_DESCRIPTOR(
			kPascalStackBased | STACK_ROUTINE_PARAMETER(1, kFourByteCode),
			notificationProc);

		static NMRec rec = {
			NULL, // qLink
			8, // qType
			0, 0, 0, // reserved fields
			0, NULL, NULL, NULL, // nmMark, nmIcon, nmSound, nmStr
			&descriptor
		};

		NMInstall(&rec);
		pending_notification = true;
	}
}

// Got some system task time to access the Toolbox safely
// TN1033 suggests using the Notification Manager
static void notificationProc(NMRecPtr nmReqPtr) {
	ATOMIC1(notificationAtomic, nmReqPtr);
}

// Now, in addition to the Toolbox being safe, interrupts are (mostly) disabled
static void notificationAtomic(NMRecPtr nmReqPtr) {
	struct virtio_gpu_config *config = VConfig;
	short width, height;
	unsigned long newdepth = depth;

	NMRemove(nmReqPtr);
	pending_notification = false;

	if ((config->events_read & VIRTIO_GPU_EVENT_DISPLAY) == 0) return;

	config->events_clear = VIRTIO_GPU_EVENT_DISPLAY;
	SynchronizeIO();

	getBestSize(&width, &height);
	if (W == width && H == height) return;

	// Kick the Display Manager
	DMSetDisplayMode(DMGetFirstScreenDevice(true),
		idForRes(width, height, true), &newdepth, 0, NULL);
}

void DNotified(uint16_t q, size_t len, void *tag) {
	last_tag = tag;
	if ((unsigned long)tag < 256) {
		freebufs |= 1 << (char)(uint32_t)tag;
		sendPixels(0x7fff7fff, 0x00000000);
	}
}

static void debugPoll(void) {
	// If we enter the debugger with all descriptors in flight (rare),
	// we will stall waiting for an interrupt to free up a descriptor.
	while (freebufs == 0) QPoll(0);

	updateScreen(0, 0, H, W);
}

static void lateBootHook(void) {
	InstallDirtyRectPatch();
	updateScreen(0, 0, H, W);
	DConfigChange();
	printf("Patched QuickDraw to capture draw events\n");
}

void DirtyRectCallback(short top, short left, short bottom, short right) {
	qdworks = true;

	top = MAX(MIN(top, H), 0);
	bottom = MAX(MIN(bottom, H), 0);
	left = MAX(MIN(left, W), 0);
	right = MAX(MIN(right, W), 0);

	if (top >= bottom || left >= right) return;

	updateScreen(top, left, bottom, right);
}

// Caller must not give out-of-range coords!
static void updateScreen(short t, short l, short b, short r) {
	short drawn_l=l, drawn_r=r;

	if (change_in_progress) return;

	Blit(depth - k1bit,
		t, &drawn_l, b, &drawn_r,
		backbuf, frontbuf, rowbytes_back,
		private_clut, gamma_red, gamma_grn, gamma_blu);

	// Any overlap with the cursor? Redraw the cursor.
	if (curs_visible && curs_l < drawn_r && drawn_l < curs_r && curs_t < b && t < curs_b) {
		// Does the cursor need a clean background to draw on?
		// And is the background overlap incomplete?
		if (curs_inverts && (curs_l < drawn_l || drawn_r < curs_r || curs_t < t || b < curs_b)) {
			short my_curs_l = curs_l, my_curs_r = curs_r;
			Blit(depth - k1bit,
				curs_t, &my_curs_l, curs_b, &my_curs_r,
				backbuf, frontbuf, rowbytes_back,
				private_clut, gamma_red, gamma_grn, gamma_blu);
		}

		blitCursor();
	}

	ATOMIC2(sendPixels,
		(((unsigned long)t << 16) | l),
		(((unsigned long)b << 16) | r));
}

// Non-reentrant, must be called atomically
// MacsBug time might be an exception
// Kick at interrupt time: sendPixels(0x7fff7fff, 0x00000000);
static void sendPixels(uint32_t topleft, uint32_t botright) {
	static bool reentered;
	static bool interest;

	// Stored pending rect
	static short ptop = 0x7fff;
	static short pleft = 0x7fff;
	static short pbottom = 0;
	static short pright = 0;

	short top = topleft >> 16;
	short left = topleft;
	short bottom = botright >> 16;
	short right = botright;

	int i;

	struct virtio_gpu_transfer_to_host_2d *obuf1; // 56 bytes
	uint32_t physicals1[2];
	uint32_t sizes1[2] = {56, 24}; // obuf1 and virtio_gpu_ctrl_hdr

	struct virtio_gpu_resource_flush *obuf2;      // 48 bytes
	uint32_t physicals2[2];
	uint32_t sizes2[2] = {48, 24}; // obuf2 and virtio_gpu_ctrl_hdr

	// We have been reentered via QPoll and DNotified -- nothing to do
	if (reentered) return;

	// Union the pending rect and the passed-in rect
	top = MIN(top, ptop);
	left = MIN(left, pleft);
	bottom = MAX(bottom, pbottom);
	right = MAX(right, pright);
	if (top >= bottom || left >= right) return;

	// Enable queue notifications so that none are missed after QPoll
	if (!interest) {
		interest = true;
		QInterest(0, 1);
	}

	// Might reenter this routine, in which case must return early (above)
	reentered = true;
	QPoll(0);
	reentered = false;

	// Now we are guaranteed that a free buffer won't be missed (unless we turn off rupts)

	// No free buffer yet... return and wait for a queue notification
	if (!freebufs) {
		ptop = top;
		pleft = left;
		pbottom = bottom;
		pright = right;
		return;
	}

	// We have a free buffer, so don't need to wait for a notification to provide one
	interest = false;
	QInterest(0, -1);

	// Pick a buffer
	for (i=0; i<maxinflight; i++) {
		if (freebufs & (1 << i)) {
			freebufs &= ~(1 << i);
			break;
		}
	}

	// The 4096-byte page is divided into 192-byte blocks for locality
	obuf1 = (void *)((char *)lpage + 192*i);
	obuf2 = (void *)((char *)obuf1 + 64);

	physicals1[0] = ppage + 192*i;
	physicals1[1] = physicals1[0] + 128;
	physicals2[0] = physicals1[0] + 64;
	physicals2[1] = physicals1[0] + 160;

	// Update the host resource from guest memory.
	obuf1->hdr.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
	obuf1->hdr.flags = 0;
	obuf1->r.x = left;
	obuf1->r.y = top;
	obuf1->r.width = right - left;
	obuf1->r.height = bottom - top;
	obuf1->offset = top*rowbytes_front + left*4;
	obuf1->offset_hi = 0;
	obuf1->resource_id = screen_resource;

	QSend(0, 1, 1, physicals1, sizes1, (void *)'tfer');

	// Flush the updated resource to the display.
	obuf2->hdr.type = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
	obuf2->hdr.flags = 0;
	obuf2->r.x = left;
	obuf2->r.y = top;
	obuf2->r.width = right - left;
	obuf2->r.height = bottom - top;
	obuf2->resource_id = screen_resource;

	QSend(0, 1, 1, physicals2, sizes2, (void *)i);
	QNotify(0);

	ptop = 0x7fff;
	pleft = 0x7fff;
	pbottom = 0;
	pright = 0;
}

static void perfTest(void) {
	// control-shift
	KeyMap keys;
	GetKeys(keys);
	if ((keys[1]&9) == 9) {
		logenable = 1; // enable debug printing from now on
		printf("Switching to %dx%dx%d. Speed test:\n", W, H, 1<<(depth-kDepthMode1));
	} else {
		printf("Switching to %dx%dx%d. Ctrl-shift for speed test.\n", W, H, 1<<(depth-kDepthMode1));
		return;
	}

	long t=LMGetTicks();
	long ctr1=0, ctr2=0;

	// Warm up
	t += 2;
	while (t > LMGetTicks()) {
		while (freebufs == 0) QPoll(0);
		updateScreen(0, 0, H, W);
	}

	// Measure with our blitter involved
	t += 30;
	while (t > LMGetTicks()) {
		while (freebufs == 0) QPoll(0);
		updateScreen(0, 0, H, W);
		ctr1++;
	}

	// Warm up
	t += 2;
	while (t > LMGetTicks()) {
		while (freebufs == 0) QPoll(0);
		ATOMIC2(sendPixels, 0x00000000, 0x7fff7fff);
	}

	// Measure without our blitter
	t += 30;
	while (t > LMGetTicks()) {
		while (freebufs == 0) QPoll(0);
		ATOMIC2(sendPixels, 0x00000000, 0x7fff7fff);
		ctr2++;
	}

	printf("%ld Hz with gamma correction, %ld Hz without\n", ctr1*2, ctr2*2);
}

static OSStatus VBL(void *p1, void *p2) {
	if (vblon) {
		//if (*(signed char *)0x910 >= 0) Debugger();
		VSLDoInterruptService(vblservice);
	}

	updateScreen(0, 0, H, W);

	vbltime = AddDurationToAbsolute(qdworks ? SLOW_REFRESH : FAST_REFRESH, vbltime);
	SetInterruptTimer(&vbltime, VBL, NULL, &vbltimer);

	return noErr;
}

// For QuickDraw
static long rowbytesForBack(int relativeDepth, long width) {
	long bppshift, bytealign, size;

	// log2(bits per pixel)
	bppshift = relativeDepth - k1bit;

	// Fewest bytes that can hold the pixels
	size = ((width << bppshift) + 7) / 8;

	// Extra alignment according to table in blit.c
	bytealign = BlitterAlign[relativeDepth - k1bit];
	size = (size + bytealign - 1) & -bytealign;

	return size;
}

// Virtio only requires 1-pixel i.e. 4-byte alignment, which is automatic,
// but to simplify the blitters we match QuickDraw's "wasted" right-edge pixels.
// This means the ratio between the sizes is a simple bit-shift.
static long rowbytesForFront(int relativeDepth, long width) {
	return rowbytesForBack(relativeDepth, width)
		<< (k32bit - relativeDepth);
}

// Update private_clut from public_clut
static void reCLUT(int index) {
	private_clut[index] =
		((uint32_t)gamma_red[public_clut[index].rgb.red >> 8] << 8) |
		((uint32_t)gamma_grn[public_clut[index].rgb.green >> 8] << 16) |
		((uint32_t)gamma_blu[public_clut[index].rgb.blue >> 8] << 24);
}

static void grayPattern(void) {
	short x, y;
	uint32_t value;

	if (depth <= k16bit) {
		if (depth == k1bit) {
			value = 0x55555555;
		} else if (depth == k2bit) {
			value = 0x33333333;
		} else if (depth == k4bit) {
			value = 0x0f0f0f0f;
		} else if (depth == k8bit) {
			value = 0x00ff00ff;
		} else if (depth == k16bit) {
			value = 0x0000ffff;
		}

		for (y=0; y<H; y++) {
			for (x=0; x<rowbytes_back; x+=4) {
				*(uint32_t *)((char *)backbuf + y*rowbytes_back + x) = value;
			}
			value = ~value;
		}
	} else if (depth == k32bit) {
		value = 0x00000000;
		for (y=0; y<H; y++) {
			for (x=0; x<rowbytes_back; x+=8) {
				*(uint32_t *)((char *)backbuf + y*rowbytes_back + x) = value;
				*(uint32_t *)((char *)backbuf + y*rowbytes_back + x + 4) = ~value;
			}
			value = ~value;
		}
	}
}

static void grayCLUT(void) {
	int i;

	if (depth > k8bit) return;

	for (i=0; i<256; i++) {
		public_clut[i].rgb.red = 0x7fff;
		public_clut[i].rgb.green = 0x7fff;
		public_clut[i].rgb.blue = 0x7fff;
		reCLUT(i);
	}
}

static void linearCLUT(void) {
	int i, j, bppshift, bpp, colors;
	uint16_t luma;

	if (depth > k8bit) return;

	bppshift = depth - k1bit;
	bpp = 1 << bppshift;
	colors = 1 << bpp;

	for (i=0; i<colors; i++) {
		// Stretch the 1/2/4/8-bit code into 16 bits
		luma = 0;
		for (j=0; j<16; j++)
			luma = (luma << bpp) | i;

		public_clut[i].rgb.red = luma;
		public_clut[i].rgb.green = luma;
		public_clut[i].rgb.blue = luma;
		reCLUT(i);
	}
}

static void setGammaTable(GammaTbl *tbl) {
	void *data = (char *)tbl + 12 + tbl->gFormulaSize;
	long size = 12 +
		tbl->gFormulaSize +
		(long)tbl->gChanCnt * tbl->gDataCnt * tbl->gDataWidth / 8;
	int i, j;

	memcpy(gamma_public, tbl, size);

	// red, green, blue
	for (i=0; i<3; i++) {
		uint8_t *src, *dst;
		if (tbl->gChanCnt == 3) {
			src = (uint8_t *)data + i * tbl->gDataCnt * tbl->gDataWidth / 8;
		} else {
			src = (uint8_t *)data;
		}

		if (i == 0) {
			dst = (void *)gamma_red;
		} else if (i == 1) {
			dst = (void *)gamma_grn;
		} else {
			dst = (void *)gamma_blu;
		}

		// Calculate and report approximate exponent
		// {
		// 	const char colors[] = "RGB";
		// 	double middle = ((double)src[127] + (double)src[128]) / 2.0 / 255.0;
		// 	double exponent = -log2(middle);
		// 	printf("Approximate %c exponent = %.3f\n", colors[i], exponent);
		// }

		for (j=0; j<256 && j<tbl->gDataCnt; j++) {
			dst[j] = src[j * tbl->gDataWidth / 8];
		}
	}

	gammaCursor();
}

// Returns the base address of a specified page in the current mode.
// --- csMode      Unused
// --- csData      Unused
// --> csPage      Desired page
// <-- csBaseAddr  Base address of VRAM for the desired page
static OSStatus GetBaseAddr(VDPageInfo *rec) {
	if (rec->csPage != 0) return statusErr;
	rec->csBaseAddr = backbuf;
	return noErr;
}

// If the video card is an indexed device, the SetEntries control routine
// should change the contents of the card’s CLUT.
// --> csTable     Pointer to ColorSpec array
// --> csStart     First entry in table
// --> csCount     Number of entries to set
static OSStatus MySetEntries(VDSetEntryRecord *rec) {
	if (depth > k8bit) return controlErr;
	return DirectSetEntries(rec);
}

// Normally, color table animation is not used on a direct device, but
// there are some special circumstances under which an application may want
// to change the color table hardware. The DirectSetEntries routine
// provides the direct device with indexed mode functionality identical to
// the regular SetEntries control routine.
static OSStatus DirectSetEntries(VDSetEntryRecord *rec) {
	int src, dst;

	for (src=0; src<=rec->csCount; src++) {
		if (rec->csStart == -1) {
			dst = rec->csTable[src].value;
		} else {
			dst = rec->csStart + src;
		}

		public_clut[dst].rgb = rec->csTable[src].rgb;
		reCLUT(dst);
	}

	updateScreen(0, 0, H, W);

	return noErr;
}

// Returns the specified number of consecutive CLUT entries, starting with
// the specified first entry.
// <-> csTable     Pointer to ColorSpec array
// --> csStart     First entry in table
// --> csCount     Number of entries to set
static OSStatus GetEntries(VDSetEntryRecord *rec) {
	int src, dst;

	for (dst=0; dst<=rec->csCount; dst++) {
		if (rec->csStart == -1) {
			src = rec->csTable[dst].value;
		} else {
			src = rec->csStart + dst;
		}

		rec->csTable[dst] = public_clut[src];
	}

	return noErr;
}

// Not well documented, but needed by MacsBug
static OSStatus GetClutBehavior(VDClutBehavior *rec) {
	*rec = kSetClutAtVBL;
	return noErr;
}

static OSStatus SetClutBehavior(VDClutBehavior *rec) {
	return controlErr;
}

// Sets the gamma table in the driver that corrects RGB color values.
// If NIL is passed for the csGTable value, the driver should build a
// linear ramp in the gamma table to allow for an uncorrected display.
// --> csGTable    Pointer to gamma table
static OSStatus SetGamma(VDGammaRecord *rec) {
	struct myGammaTable {
		 short gVersion;           // always 0
		 short gType;              // 0 means "independent from CLUT"
		 short gFormulaSize;       // display-identifying bytes after gDataWidth = 0
		 short gChanCnt;           // 1 in this case, could be 3 in others
		 short gDataCnt;           // 256
		 short gDataWidth;         // 8 bits per element
		 unsigned char data[256];
	};

	static const struct myGammaTable uncorrectedTable = {0, 0, 0, 1, 256, 8, {
		0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,
		0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f,
		0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28,0x29,0x2a,0x2b,0x2c,0x2d,0x2e,0x2f,
		0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x3b,0x3c,0x3d,0x3e,0x3f,
		0x40,0x41,0x42,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4a,0x4b,0x4c,0x4d,0x4e,0x4f,
		0x50,0x51,0x52,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x5b,0x5c,0x5d,0x5e,0x5f,
		0x60,0x61,0x62,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x6b,0x6c,0x6d,0x6e,0x6f,
		0x70,0x71,0x72,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x7b,0x7c,0x7d,0x7e,0x7f,
		0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8a,0x8b,0x8c,0x8d,0x8e,0x8f,
		0x90,0x91,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0x9b,0x9c,0x9d,0x9e,0x9f,
		0xa0,0xa1,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xab,0xac,0xad,0xae,0xaf,
		0xb0,0xb1,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xbb,0xbc,0xbd,0xbe,0xbf,
		0xc0,0xc1,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xcb,0xcc,0xcd,0xce,0xcf,
		0xd0,0xd1,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0xdb,0xdc,0xdd,0xde,0xdf,
		0xe0,0xe1,0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xeb,0xec,0xed,0xee,0xef,
		0xf0,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa,0xfb,0xfc,0xfd,0xfe,0xff}};

	if (rec->csGTable != NULL)
		setGammaTable((GammaTbl *)rec->csGTable);
	else
		setGammaTable((GammaTbl *)&uncorrectedTable);

	if (depth <= k8bit) {
		// SetGamma on indexed devices sets a linear black-to-white CLUT,
		// and is guaranteed to be followed by a SetEntries to fix the CLUT.
		linearCLUT();
	} else {
		// What to do on direct devices?
		updateScreen(0, 0, H, W);
	}

	return noErr;
}

// Returns a pointer to the current gamma table.
// <-- csGTable    Pointer to gamma table
static OSStatus GetGamma(VDGammaRecord *rec) {
	rec->csGTable = gamma_public;
	return noErr;
}

// Clients wishing to find a graphics card’s available gamma tables
// formerly accessed the Slot Manager data structures. PCI graphics drivers
// must return this information directly.
// --> csPreviousGammaTableID  ID of the previous gamma table
// <-- csGammaTableID          ID of the gamma table following
//                             csPreviousDisplayModeID
// <-- csGammaTableSize        Size of the gamma table in bytes
// <-- csGammaTableName        Gamma table name (C string)
static OSStatus GetGammaInfoList(VDGetGammaListRec *rec) {
	enum {first = 128};
	long last = first + builtinGammaCount - 1;

	long id;

	if (rec->csPreviousGammaTableID == kGammaTableIDFindFirst) {
		id = first;
	} else if (rec->csPreviousGammaTableID == kGammaTableIDSpecific) {
		id = rec->csGammaTableID;

		if (id < first || id > last) {
			return paramErr;
		}
	} else if (rec->csPreviousGammaTableID >= first && rec->csPreviousGammaTableID < last) {
		id = rec->csPreviousGammaTableID + 1;
	} else if (rec->csPreviousGammaTableID == last) {
		rec->csGammaTableID = kGammaTableIDNoMoreTables;
		printf("GetGammaInfoList prevID=%d ... ID=%d size=%d name=%s\n",
			rec->csPreviousGammaTableID,
			rec->csGammaTableID,
			rec->csGammaTableSize,
			rec->csGammaTableName);
		return noErr;
	} else {
		return paramErr;
	}

	rec->csGammaTableID = id;
	rec->csGammaTableSize = sizeof(builtinGamma[0].table);
	memcpy(rec->csGammaTableName, builtinGamma[id-first].name, 32);

	printf("GetGammaInfoList prevID=%d ... ID=%d size=%d name=%s\n",
		rec->csPreviousGammaTableID,
		rec->csGammaTableID,
		rec->csGammaTableSize,
		rec->csGammaTableName);

	return noErr;
}

// Copies the designated gamma table into the designated location.
// --> csGammaTableID      ID of gamma table to retrieve
// <-> csGammaTablePtr     Location to copy table into
static OSStatus RetrieveGammaTable(VDRetrieveGammaRec *rec) {
	enum {first = 128};
	long last = first + builtinGammaCount - 1;

	long id = rec->csGammaTableID;

	if (id < first || id > last) {
		return paramErr;
	}

	memcpy(rec->csGammaTablePtr, &builtinGamma[id-first].table, sizeof(builtinGamma[0].table));

	return noErr;
}

// Fills the specified video page with a dithered gray pattern in the
// current video mode. The page number is 0 based.
// --- csMode      Unused
// --- csData      Unused
// --> csPage      Desired display page to gray
// --- csBaseAddr  Unused
static OSStatus GrayPage(VDPageInfo *rec) {
	if (rec->csPage != 0) return controlErr;
	grayPattern();
	updateScreen(0, 0, H, W);
	return noErr;
}

// Specify whether subsequent SetEntries calls fill a card’s CLUT with
// actual colors or with the luminance-equivalent gray tones.
// --> csMode      Enable or disable luminance mapping
static OSStatus SetGray(VDGrayRecord *rec) {
	return controlErr;
}

// Describes the behavior of subsequent SetEntries control calls to indexed
// devices.
// <-- csMode      Luminance mapping enabled or disabled
static OSStatus GetGray(VDGrayRecord *rec) {
	rec->csMode = 0;
	return noErr;
}

// Returns the total number of video pages available in the current video
// card mode, not the current page number. This is a counting number and is
// not 0 based.
// --- csMode      Unused
// --- csData      Unused
// <-- csPage      Number of display pages available
// --- csBaseAddr  Unused
static OSStatus GetPages(VDPageInfo *rec) {
	rec->csPage = 1;
	return noErr;
}

// To enable interrupts, pass a csMode value of 0; to disable interrupts,
// pass a csMode value of 1.
// --> csMode      Enable or disable interrupts
static OSStatus SetInterrupt(VDFlagRecord *rec) {
	vblon = !rec->csMode;
	return noErr;
}

// Returns a value of 0 if VBL interrupts are enabled and a value of 1 if
// VBL interrupts are disabled.
// <-- csMode      Interrupts enabled or disabled
static OSStatus GetInterrupt(VDFlagRecord *rec) {
	rec->csMode = !vblon;
	return noErr;
}

// GetSync and SetSync can be used to implement the VESA DPMS as well as
// enable a sync-on-green mode for the frame buffer.
static OSStatus SetSync(VDSyncInfoRec *rec) {
	return noErr;
}

static OSStatus GetSync(VDSyncInfoRec *rec) {
	rec->csMode = 0;
	return noErr;
}

// --> powerState  Switch display hardware to this state
// <-- powerFlags  Describes the status of the new state
static OSStatus SetPowerState(VDPowerStateRec *rec) {
	return controlErr;
}

// <-- powerState  Current power state of display hardware
// <-- powerFlags  Status of current state
static OSStatus GetPowerState(VDPowerStateRec *rec) {
	return statusErr;
}

// Save the preferred relative bit depth (depth mode) and display mode.
// This means that a PCI card should save this information in NVRAM so that
// it persists across system restarts.
// --> csMode      Relative bit depth of preferred resolution
// --> csData      DisplayModeID of preferred resolution
// --- csPage      Unused
// --- csBaseAddr  Unused
static OSStatus SavePreferredConfiguration(VDSwitchInfoRec *rec) {
	return controlErr;
}

// <-- csMode      Relative bit depth of preferred resolution
// <-- csData      DisplayModeID of preferred resolution
// --- csPage      Unused
// --- csBaseAddr  Unused
static OSStatus GetPreferredConfiguration(VDSwitchInfoRec *rec) {
	return statusErr;
}

// Gathers information about the attached display.
// <-- csDisplayType         Display type of attached display
// <-- csConnectTaggedType   Type of tagging
// <-- csConnectTaggedData   Tagging data
// <-- csConnectFlags        Connection flags
// <-- csDisplayComponent    Return display component, if available
static OSStatus GetConnection(VDDisplayConnectInfoRec *rec) {
	rec->csDisplayType = kGenericLCD;
	rec->csConnectTaggedType = 0;
	rec->csConnectTaggedData = 0;
	rec->csConnectFlags = (1 << kTaggingInfoNonStandard);
	rec->csDisplayComponent = 0;
	return noErr;
}

// Returns the current relative bit depth, page, and base address.
// <-- csMode      Current relative bit depth
// --- csData      Unused
// <-- csPage      Current display page
// <-- csBaseAddr  Base address of video RAM for the current
//                 DisplayModeID and relative bit depth
static OSStatus GetMode(VDPageInfo *rec) {
	rec->csMode = depth;
	rec->csPage = 0;
	rec->csBaseAddr = backbuf;
	return noErr;
}

// Like GetMode, except:
// PCI graphics drivers return the current DisplayModeID value in the
// csData field.
// <-- csMode      Current relative bit depth
// <-- csData      DisplayModeID of current resolution
// <-- csPage      Current page
// <-- csBaseAddr  Base address of current page
static OSStatus GetCurMode(VDSwitchInfoRec *rec) {
	rec->csMode = depth;
	rec->csData = idForRes(W, H, false);
	rec->csPage = 0;
	rec->csBaseAddr = backbuf;
	return noErr;
}

// Report timing information for the desired displayModeID.
// --> csTimingMode    Desired DisplayModeID
// <-- csTimingFormat  Format for timing info (kDeclROMtables)
// <-- csTimingData    Scan timing for desired DisplayModeID
// <-- csTimingFlags   Report whether this scan timing is optional or required
static OSStatus GetModeTiming(VDTimingInfoRec *rec) {
	if (rec->csTimingMode < 1 || rec->csTimingMode > resCount()) return paramErr;

	rec->csTimingFormat = kDeclROMtables;
	rec->csTimingData = timingApple_FixedRateLCD;
	rec->csTimingFlags = (1 << kModeValid) | (1 << kModeSafe) | (1 << kModeShowNow);
	if (rezzes[rec->csTimingMode-1].w == W && rezzes[rec->csTimingMode-1].h == H)
		rec->csTimingFlags |= (1 << kModeDefault);

	return noErr;
}

// Sets the pixel depth of the screen.
// --> csMode          Desired relative bit depth
// --- csData          Unused
// --> csPage          Desired display page
// <-- csBaseAddr      Base address of video RAM for this csMode
static OSStatus SetMode(VDPageInfo *rec) {
	if (rec->csMode < kDepthMode1 || rec->csMode > kDepthModeMax) return paramErr;
	if (rec->csPage != 0) return controlErr;

	if (!mode(rec->csMode, idForRes(W, H, false))) return paramErr;

	grayCLUT();
	perfTest();
	updateScreen(0, 0, H, W);

	rec->csBaseAddr = backbuf;
	return noErr;
}

// --> csMode          Relative bit depth to switch to
// --> csData          DisplayModeID to switch into
// --> csPage          Video page number to switch into
// <-- csBaseAddr      Base address of the new DisplayModeID
static OSStatus SwitchMode(VDSwitchInfoRec *rec) {
	if (rec->csMode < kDepthMode1 || rec->csMode > kDepthModeMax) return paramErr;
	if (rec->csData < 1 || rec->csData > resCount()) return paramErr;
	if (rec->csPage != 0) return paramErr;

	if (!mode(rec->csMode, rec->csData)) return paramErr;

	grayCLUT();
	perfTest();
	updateScreen(0, 0, H, W);

	rec->csBaseAddr = backbuf;
	return noErr;
}

// Reports all display resolutions that the driver supports.
// --> csPreviousDisplayModeID   ID of the previous display mode
// <-- csDisplayModeID           ID of the display mode following
//                               csPreviousDisplayModeID
// <-- csHorizontalPixels        Number of pixels in a horizontal line
// <-- csVerticalLines           Number of lines in a screen
// <-- csRefreshRate             Vertical refresh rate of the screen
// <-- csMaxDepthMode            Max relative bit depth for this DisplayModeID
static OSStatus GetNextResolution(VDResolutionInfoRec *rec) {
	uint32_t id;

	if (rec->csPreviousDisplayModeID == kDisplayModeIDFindFirstResolution) {
		id = 1;
	} else if (rec->csPreviousDisplayModeID >= 1 && rec->csPreviousDisplayModeID < resCount()) {
		id = rec->csPreviousDisplayModeID + 1;
	} else if (rec->csPreviousDisplayModeID == resCount()) {
		rec->csDisplayModeID = kDisplayModeIDNoMoreResolutions;
		return noErr;
	} else if (rec->csPreviousDisplayModeID == kDisplayModeIDCurrent) {
		id = idForRes(W, H, false);
	} else {
		return paramErr;
	}

	rec->csDisplayModeID = id;
	rec->csHorizontalPixels = rezzes[id-1].w;
	rec->csVerticalLines = rezzes[id-1].h;
	rec->csRefreshRate = 60;
	rec->csMaxDepthMode = k32bit;

	return noErr;
}

// --> csDisplayModeID   ID of the desired DisplayModeID
// --> csDepthMode       Relative bit depth
// <-> *csVPBlockPtr     Pointer to a VPBlock
// <-- csPageCount       Number of pages supported for resolution
//                       and relative bit depth
// <-- csDeviceType      Direct, fixed, or CLUT
static OSStatus GetVideoParameters(VDVideoParametersInfoRec *rec) {
	if (rec->csDepthMode < kDepthMode1 || rec->csDepthMode > kDepthModeMax) {
		return paramErr;
	}

	if (rec->csDisplayModeID < 1 || rec->csDisplayModeID > sizeof(rezzes)/sizeof(*rezzes)) {
		return paramErr;
	}

	memset(rec->csVPBlockPtr, 0, sizeof(*rec->csVPBlockPtr));

	// These fields are always left at zero:
	// vpBaseOffset (offset from NuBus slot to first page, always zero for us)
	// vpBounds.topLeft vpVersion vpPackType vpPackSize vpPlaneBytes

	// These fields don't change per mode:
	rec->csPageCount = 1;
	rec->csVPBlockPtr->vpHRes = 0x00480000;	// Hard coded to 72 dpi
	rec->csVPBlockPtr->vpVRes = 0x00480000;	// Hard coded to 72 dpi

	rec->csVPBlockPtr->vpBounds.bottom = rezzes[rec->csDisplayModeID-1].h;
	rec->csVPBlockPtr->vpBounds.right = rezzes[rec->csDisplayModeID-1].w;
	rec->csVPBlockPtr->vpRowBytes = rowbytesForBack(rec->csDepthMode, rezzes[rec->csDisplayModeID-1].w);

	switch (rec->csDepthMode) {
	case k1bit:
		rec->csVPBlockPtr->vpPixelType = 0; // indexed
		rec->csVPBlockPtr->vpPixelSize = 1;
		rec->csVPBlockPtr->vpCmpCount = 1;
		rec->csVPBlockPtr->vpCmpSize = 1;
		rec->csDeviceType = clutType;
		break;
	case k2bit:
		rec->csVPBlockPtr->vpPixelType = 0; // indexed
		rec->csVPBlockPtr->vpPixelSize = 2;
		rec->csVPBlockPtr->vpCmpCount = 1;
		rec->csVPBlockPtr->vpCmpSize = 2;
		rec->csDeviceType = clutType;
		break;
	case k4bit:
		rec->csVPBlockPtr->vpPixelType = 0; // indexed
		rec->csVPBlockPtr->vpPixelSize = 4;
		rec->csVPBlockPtr->vpCmpCount = 1;
		rec->csVPBlockPtr->vpCmpSize = 4;
		rec->csDeviceType = clutType;
		break;
	case k8bit:
		rec->csVPBlockPtr->vpPixelType = 0; // indexed
		rec->csVPBlockPtr->vpPixelSize = 8;
		rec->csVPBlockPtr->vpCmpCount = 1;
		rec->csVPBlockPtr->vpCmpSize = 8;
		rec->csDeviceType = clutType;
		break;
	case k16bit:
		rec->csVPBlockPtr->vpPixelType = 16; // direct
		rec->csVPBlockPtr->vpPixelSize = 16;
		rec->csVPBlockPtr->vpCmpCount = 3;
		rec->csVPBlockPtr->vpCmpSize = 5;
		rec->csDeviceType = directType;
		break;
	case k32bit:
		rec->csVPBlockPtr->vpPixelType = 16; // direct
		rec->csVPBlockPtr->vpPixelSize = 32;
		rec->csVPBlockPtr->vpCmpCount = 3;
		rec->csVPBlockPtr->vpCmpSize = 8;
		rec->csDeviceType = directType;
		break;
	}

	return noErr;
}

// Graphics drivers that support hardware cursors must return true.
// <-- csSupportsHardwareCursor  true if hardware cursor is supported
static OSStatus SupportsHardwareCursor(VDSupportsHardwareCursorRec *rec) {
	rec->csSupportsHardwareCursor = 1;
	return noErr;
}

// The csCursorSet parameter should be true if the last SetHardwareCursor
// control call was successful and false otherwise. If csCursorSet is true,
// the csCursorX, csCursorY, and csCursorVisible values must match the
// parameters passed in to the last DrawHardwareCursor control call.
// <-- csCursorX           X coordinate from last DrawHardwareCursor call
// <-- csCursorY           Y coordinate from last DrawHardwareCursor call
// <-- csCursorVisible     true if the cursor is visible
// <-- csCursorSet         true if cursor was successfully set by the last
//                         SetHardwareCursor call
static OSStatus GetHardwareCursorDrawState(VDHardwareCursorDrawStateRec *rec) {
	rec->csCursorX = curs_l;
	rec->csCursorY = curs_t;
	rec->csCursorVisible = curs_visible;
	rec->csCursorSet = curs_set;
	return noErr;
}

// QuickDraw uses the SetHardwareCursor control call to set up the hardware
// cursor and determine whether the hardware can support it. The driver
// must determine whether it can support the given cursor and, if so,
// program the hardware cursor frame buffer (or equivalent), set up the
// CLUT, and return noErr. If the driver cannot support the cursor it must
// return controlErr. The driver must remember whether this call was
// successful for subsequent GetHardwareCursorDrawState or
// DrawHardwareCursor calls, but should not change the cursor’s x or y
// coordinates or its visible state.
//  --> csCursorRef    Reference to cursor data
static OSStatus SetHardwareCursor(VDSetHardwareCursorRec *rec) {
	int i;
	int x, y;

	enum {CLUTSIZE = 256};

	struct CursorColorTable {
		long ctSeed;
		short ctFlags;
		short ctSize;
		ColorSpec ctTable[CLUTSIZE];
	};
	static struct CursorColorTable curs_clut;
	static uint32_t curs_bmp_values[CLUTSIZE]; // populated 0...255 below

	static HardwareCursorDescriptorRec curs_desc = {
		kHardwareCursorDescriptorMajorVersion,
		kHardwareCursorDescriptorMinorVersion,
		CURSOREDGE, CURSOREDGE, // height, width
		sizeof(curs_back[0])*8, // bitDepth
		0, // maskBitDepth (reserved)
		CLUTSIZE, // numColors (excluding the transparent and invert ones)
		curs_bmp_values,
		0, // flags
		kTransparentEncodedPixel | kInvertingEncodedPixel, // supportedSpecialEncodings
		{0x80000000, 0x40000000}
	};

	static HardwareCursorInfoRec curs_struct = {
		kHardwareCursorInfoMajorVersion,
		kHardwareCursorInfoMinorVersion,
		0, 0, // height, width
		(ColorTable *)&curs_clut, // colorMap
		(void *)curs_back
	};

	for (i=0; i<sizeof(curs_bmp_values)/sizeof(*curs_bmp_values); i++) {
		curs_bmp_values[i] = i;
	}

	if (!VSLPrepareCursorForHardwareCursor(rec->csCursorRef, &curs_desc, &curs_struct)) {
		curs_set = false;
		return controlErr;
	}

	curs_r = curs_l + curs_struct.cursorWidth;
	curs_b = curs_t + curs_struct.cursorHeight;

	// Stretch a potentially smaller image out to 16x16
	for (y=CURSOREDGE-1; y>=0; y--) {
		for (x=CURSOREDGE-1; x>=0; x--) {
			if (x < curs_struct.cursorWidth && y < curs_struct.cursorHeight) {
				curs_back[CURSOREDGE*y + x] = curs_back[curs_struct.cursorWidth*y + x];
			} else {
				curs_back[CURSOREDGE*y + x] = 0x80000000; // transparent
			}
		}
	}

	curs_r = curs_l;
	curs_b = curs_t;
	curs_inverts = false;

	// Look up each pixel in the CLUT
	// Byte-swap from CRGB to BGRC; C=special-code
	for (y=0; y<CURSOREDGE; y++) {
		for (x=0; x<CURSOREDGE; x++) {
			uint32_t *pixel = &curs_back[CURSOREDGE*y + x];

			// While we're looping, get the bounds of nontransparent pixels
			if ((*pixel & 0x80000000) == 0) {
				curs_r = MAX(curs_r, curs_l + x + 1);
				curs_b = curs_t + y + 1;
			}

			// And flag whether the cursor inverts its background
			if ((*pixel & 0x40000000) == 0) {
				curs_inverts = true;
			}

			if (*pixel & 0xff000000) {
				*pixel >>= 24;
			} else {
				*pixel =
					((uint32_t)curs_clut.ctTable[*pixel].rgb.red >> 8 << 8) |
					((uint32_t)curs_clut.ctTable[*pixel].rgb.green >> 8 << 16) |
					((uint32_t)curs_clut.ctTable[*pixel].rgb.blue >> 8 << 24);
			}
		}
	}

	gammaCursor();

	curs_set = true;
	return noErr;
}

// Sets the cursor’s x and y coordinates and visible state. If the cursor
// was successfully set by a previous call to SetHardwareCursor, the driver
// must program the hardware with the given x, y, and visible parameters
// and then return noErr. If the cursor was not successfully set by the
// last SetHardwareCursor call, the driver must return controlErr.
// --> csCursorX           X coordinate
// --> csCursorY           Y coordinate
// --> csCursorVisible     true if the cursor must be visible
static OSStatus DrawHardwareCursor(VDDrawHardwareCursorRec *rec) {
	short t=0x7fff, l=0x7fff, b=0, r=0;

	if (!curs_set) return controlErr;

	// Erase the old one
	if (curs_visible) {
		t = MIN(t, curs_t);
		l = MIN(l, curs_l);
		b = MAX(b, curs_b);
		r = MAX(r, curs_r);
	}

	curs_visible = rec->csCursorVisible;
	curs_b += rec->csCursorY - curs_t;
	curs_r += rec->csCursorX - curs_l;
	curs_t = rec->csCursorY;
	curs_l = rec->csCursorX;

	// Paint the new one
	if (curs_visible) {
		t = MIN(t, curs_t);
		l = MIN(l, curs_l);
		b = MAX(b, curs_b);
		r = MAX(r, curs_r);
	}

	// Never call updateScreen out of bounds
	if (t != 0x7fff) {
		updateScreen(MAX(t, 0), MAX(l, 0), MIN(b, H), MIN(r, W));
	}

	return noErr;
}

static void gammaCursor(void) {
	int i;

	for (i=0; i<sizeof(curs_back)/sizeof(*curs_back); i++) {
		uint32_t pixel = curs_back[i];

		// Keep in BGRC format
		if ((pixel & 0xff) == 0) {
			pixel =
				((uint32_t)gamma_red[(pixel >> 8) & 0xff] << 8) |
				((uint32_t)gamma_grn[(pixel >> 16) & 0xff] << 16) |
				((uint32_t)gamma_blu[(pixel >> 24) & 0xff] << 24);
		}

		curs_front[i] = pixel;
	}
}

// Copy cursor to front buffer
static void blitCursor(void) {
	short x, y;

	// Primitively blit the cursor
	for (y=MAX(0, -curs_t); y<MIN(CURSOREDGE, H-curs_t); y++) {
		for (x=MAX(0, -curs_l); x<MIN(CURSOREDGE, W-curs_l); x++) {
			uint32_t pixel = curs_front[CURSOREDGE*y + x];
			uint32_t *target = (uint32_t *)((char *)frontbuf + rowbytes_front*(curs_t+y) + 4*(curs_l+x));
			if (pixel & 0x80) {
				// transparent, do nothing
			} else if (pixel & 0x40) {
				// invert
				*target ^= 0xffffff00;
			} else {
				*target = pixel;
			}
		}
	}
}
