/* Copyright (c) 2023 Elliot Nunn */
/* Licensed under the MIT license */

#include <Devices.h>
#include <DriverServices.h>
#include <Events.h>
#include <LowMem.h>
#include <MixedMode.h>
#include <Quickdraw.h>
#include <Types.h>
#include <Traps.h>

#include "allocator.h"
#include "callupp.h"
#include "printf.h"
#include "panic.h"
#include "transport.h"
#include "virtqueue.h"
#include "patch68k.h"

#include "device.h"

#include <string.h>

struct event {
	int16_t type;
	int16_t code;
	int32_t value;
} __attribute((scalar_storage_order("little-endian")));

typedef void (*GNEFilterType)(EventRecord *event, Boolean *result);

short funnel(long commandCode, void *pb);
static OSStatus finalize(DriverFinalInfo *info);
static OSStatus initialize(DriverInitInfo *info);
static void handleEvent(struct event e);
static void myGNEFilter(EventRecord *event, Boolean *result);
static void lateBootHook(void);
static pascal ControlPartCode myTrackControl(ControlRef theControl, Point startPoint, ControlActionUPP actionProc);
static void reQueue(int bufnum);

static struct event *lpage;
static uint32_t ppage;
static bool patchReady;
static void *oldGNEFilter, *oldTrackControl;
static ControlRecord **curScroller;
static long pendingScroll;
// uint32_t eventPostedTime;

// Work around a ROM bug:
// If kDriverIsLoadedUponDiscovery is set, the ROM calls GetDriverDescription
// for a pointer to the global below, then frees it with DisposePtr. Padding
// the global to a positive offset within our global area defeats DisposePtr.
char BugWorkaroundExport1[] = "TheDriverDescription must not come first";

DriverDescription TheDriverDescription = {
	kTheDescriptionSignature,
	kInitialDriverDescriptor,
	{"\x0cpci1af4,1052", {0x00, 0x10, 0x80, 0x00}}, // v0.1
	{kDriverIsLoadedUponDiscovery |
		kDriverIsOpenedUponLoad,
		"\x0c.VirtioInput"},
	{1, // nServices
	{{kServiceCategoryNdrvDriver, kNdrvTypeIsGeneric, {0x00, 0x10, 0x80, 0x00}}}} //v0.1
};

char BugWorkaroundExport2[] = "TheDriverDescription must not come first";

const unsigned short drvrFlags = dNeedLockMask|dStatEnableMask|dCtlEnableMask;
const char drvrNameVers[] = "\x0c.VirtioInput\0\x01\x00";

OSStatus DoDriverIO(AddressSpaceID spaceID, IOCommandID cmdID,
	IOCommandContents pb, IOCommandCode code, IOCommandKind kind) {
	OSStatus err;

	logenable = 1;

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
		err = controlErr;
		break;
	case kStatusCommand:
		err = statusErr;
		break;
	case kOpenCommand:
	case kCloseCommand:
		err = noErr;
		break;
	case kReadCommand:
		err = readErr;
		break;
	case kWriteCommand:
		err = writErr;
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

static OSStatus finalize(DriverFinalInfo *info) {
	return noErr;
}

static OSStatus initialize(DriverInitInfo *info) {
	sprintf(logprefix, "%.*s(%d) ", *drvrNameVers, drvrNameVers+1, info->refNum);
// 	if (0 == RegistryPropertyGet(&info->deviceEntry, "debug", NULL, 0)) {
		logenable = 1;
// 	}

	printf("Starting\n");

	if (!VInit(&info->deviceEntry)) {
		printf("Transport layer failure\n");
		VFail();
		return openErr;
	};

	lpage = AllocPages(1, &ppage);
	if (lpage == NULL) {
		printf("Memory allocation failure\n");
		VFail();
		return openErr;
	}

	if (!VFeaturesOK()) {
		printf("Feature negotiation failure\n");
		VFail();
		return openErr;
	}

	VDriverOK();

	int nbuf = QInit(0, 4096 / sizeof (struct event));
	if (nbuf == 0) {
		printf("Virtqueue layer failure\n");
		VFail();
		return openErr;
	}

	QInterest(0, 1); // enable interrupts
	for (int i=0; i<nbuf; i++) {
		reQueue(i);
	}
	QNotify(0);

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
}

static void lateBootHook(void) {
	printf("Patching TrackControl\n");

	oldTrackControl = (ControlActionUPP)GetToolTrapAddress(_TrackControl);

	SetToolTrapAddress(
		STATICDESCRIPTOR(
			myTrackControl,
			kPascalStackBased
				| STACK_ROUTINE_PARAMETER(1, kFourByteCode)
				| STACK_ROUTINE_PARAMETER(2, kFourByteCode)
				| STACK_ROUTINE_PARAMETER(3, kFourByteCode)
				| RESULT_SIZE(kTwoByteCode)),
		_TrackControl);

	printf("Patching GNEFilter\n");

#if GENERATINGCFM
	oldGNEFilter = LMGetGNEFilter(); // PowerPC version calls through the chain

	LMSetGNEFilter((void *)STATICDESCRIPTOR(myGNEFilter, uppGetNextEventFilterProcInfo));
#else
	Patch68k(
		0x29a, // jGNEFilter:
		"486f 0004" // pea    4(sp)         ; push boolean pointer
		"2f09"      // move.l a1,-(sp)      ; push event record pointer
		"4eb9 %l"   // jsr    myGNEFilter   ; call into C
		"225f"      // move.l (sp)+,a1      ; restore event record pointer
		"588f"      // addq   #4,sp         ; pop boolean pointer
		"302f 0004" // move.w 4(sp),d0      ; same value expected in these places
		"4ef9 %o",  // jmp    original
		myGNEFilter
	);
#endif

	patchReady = true;
}

static void handleEvent(struct event e) {
	enum {
		EV_SYN = 0,
		EV_KEY = 1,
		EV_REL = 2,
		EV_ABS = 3,

		BTN_LEFT = 272,
		BTN_RIGHT = 273,
		BTN_GEAR_DOWN = 336,
		BTN_GEAR_UP = 337,

		REL_WHEEL = 8,

		ABS_X = 0,
		ABS_Y = 1,
	};


	enum {MVFLAG = 1};

	static bool knowpos;
	static long x, y;

	static int knowmask, newbtn, oldbtn;

	// Using a macOS Qemu host, each pixel of desired scroll returns both:
	// type=EV_REL code=REL_WHEEL value=0/1
	// type=EV_KEY code=BTN_GEAR_DOWN/BTN_GEAR_UP value=0
	// But actually I would prefer the new-ish "REL_WHEEL_HI_RES"!

	if (e.type == EV_ABS && e.code == ABS_X) {
		knowpos = true;
		x = e.value;
	} else if (e.type == EV_ABS && e.code == ABS_Y) {
		knowpos = true;
		y = e.value;
	} else if (e.type == 1 && e.code == BTN_LEFT) {
		knowmask |= 1;
		if (e.value) newbtn |= 1;
	} else if (e.type == 1 && e.code == BTN_RIGHT) {
		knowmask |= 2;
		if (e.value) newbtn |= 2;
	} else if (e.type == EV_REL && e.code == REL_WHEEL && patchReady) {
		pendingScroll += e.value;

		uint32_t t = LMGetTicks();
// 		if (eventPostedTime == 0 || t - eventPostedTime > 30) {
			PostEvent(mouseDown, 'scrl');
			PostEvent(mouseUp, 'scrl');
// 			eventPostedTime = t;
// 		}
	} else if (e.type == EV_SYN) {
		if (knowpos) {
			// Scale to screen size (in lowmem globals)
			unsigned long realx = x * *(int16_t *)0xc20 / 0x8000 + 1;
			unsigned long realy = y * *(int16_t *)0xc22 / 0x8000 + 1;

			unsigned long point = (realy << 16) | realx;

			*(unsigned long *)0x828 = point; // MTemp
			*(unsigned long *)0x82c = point; // RawMouse

			*(char *)0x8ce = *(char *)0x8cf; // CrsrNew = CrsrCouple

			// Call JCrsrTask to redraw the cursor immediately.
			// Feels much more responsive than waiting for another interrupt.
			// Could a race condition garble the cursor? Haven't seen it happen.
			// if (*(char *)(0x174 + 7) & 1) // Uncomment to switch on shift key
			CALL0(void, *(void **)0x8ee);
		}

		knowpos = false;

		newbtn = (newbtn & knowmask) | (oldbtn & ~knowmask);

		if ((oldbtn != 0) != (newbtn != 0)) {
			*(unsigned char *)0x172 = newbtn ? 0 : 0x80;

			EvQEl *osevent;
			PPostEvent(newbtn ? mouseDown : mouseUp, 0, &osevent);

			// Right-click becomes control-click
			if (newbtn & 2) {
				osevent->evtQModifiers |= 0x1000;
			}
		}

		oldbtn = newbtn;
		knowmask = 0;
		newbtn = 0;
	}
}

// Second stage of scroll event
// (Now, at non-interrupt time, we can safely inspect Toolbox structures.)
// Synthesise a click event in an appropriate scrollbar.
static void myGNEFilter(EventRecord *event, Boolean *result) {
	if (event->what == mouseDown && event->message == 'scrl') {
		struct WindowRecord *wind = (void *)FrontWindow();
		unsigned char *name = *wind->titleHandle;

		// Find the scroller to move
		// Currently it's the first vertical in the front window
		// In future select more smartly
		for (curScroller = (void *)wind->controlList; curScroller; curScroller = (**curScroller).nextControl) {
			struct ControlRecord *ptr = *curScroller;

			void *defproc = *ptr->contrlDefProc;
			short cdefnum = *(int16_t *)(defproc + 8);

			int16_t w = ptr->contrlRect.right - ptr->contrlRect.left;
			int16_t h = ptr->contrlRect.bottom - ptr->contrlRect.top;

			if (h > w && ptr->contrlHilite != 255 && cdefnum == 24) break;
		}

		if (curScroller) {
			// Yes, it's the right event
			// Click in the upper-left of the scroller
			Point pt = {(*curScroller)->contrlRect.top, (*curScroller)->contrlRect.left};

			// Convert to window coordinates
			GrafPtr oldport;
			GetPort(&oldport);
			SetPort((*curScroller)->contrlOwner);
			LocalToGlobal(&pt);
			SetPort(oldport);

			event->what = mouseDown;
			event->message = 0;
			event->where = pt;
			*result = true;
		} else {
			// Not the right event
			event->what = nullEvent;
			event->message = 0;
			*result = false;
		}
	}

	// On PowerPC, call the next filter in the chain
	// On 68k, just return, and the wrapper code will jump to it
#if GENERATINGCFM
	CallGetNextEventFilterProc(oldGNEFilter, event, result);
#endif
}

// Third stage of scroll event
// The app associated the (fake) click with our scrollbar
// Tell the app that the scrollbar was dragged,
// but tell the scrollbar to move to a specific point.
static pascal ControlPartCode myTrackControl(ControlRef theControl, Point startPoint, ControlActionUPP actionProc) {
	if (theControl != curScroller) {
		return CALLPASCAL3(ControlPartCode, oldTrackControl,
			ControlRef, theControl,
			Point, startPoint,
			ControlActionUPP, actionProc);
	}

	int32_t min, max, val;
// COMMENT OUT BECAUSE IT PREVENTS US LOADING AT EARLY BOOT!
// #if GENERATINGCFM
// 	if (GetControl32BitValue != NULL) {
// 		min = GetControl32BitMinimum(curScroller);
// 		max = GetControl32BitMaximum(curScroller);
// 		val = GetControl32BitValue(curScroller);
// 	} else
// #endif
	{
		min = GetControlMinimum(curScroller);
		max = GetControlMaximum(curScroller);
		val = GetControlValue(curScroller);
	}


	val -= pendingScroll * 3;
	pendingScroll = 0;
	if (val < min) val = min;
	if (val > max) val = max;
	SetControlValue(curScroller, val); // ALSO DO 32-BIT!

	if ((int)actionProc & 1) {
		actionProc = (*curScroller)->contrlAction;
	}

	if (actionProc) {
		CALLPASCAL2(void, actionProc,
			ControlRef, curScroller,
			short, 129);
	}

	curScroller = NULL;

	return 129; // click was in the thumb
}

static void reQueue(int bufnum) {
	QSend(0, 0/*n-send*/, 1/*n-recv*/,
		(uint32_t []){ppage + sizeof (struct event) * bufnum},
		(uint32_t []){sizeof (struct event)},
		(void *)bufnum);
}

void DNotified(uint16_t q, size_t len, void *tag) {
	handleEvent(lpage[(int)tag]);
	reQueue((int)tag);
	QNotify(0);
}

void DConfigChange(void) {
}
