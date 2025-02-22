/* Copyright (c) 2023 Elliot Nunn */
/* Licensed under the MIT license */

#include <Devices.h>
#include <DriverServices.h>
#include <Events.h>
#include <LowMem.h>
#include <MixedMode.h>
#include <Quickdraw.h>
#include <Types.h>

#include "allocator.h"
#include "callout68k.h"
#include "cleanup.h"
#include "extralowmem.h"
#include "printf.h"
#include "panic.h"
#include "transport.h"
#include "scrollwheel.h"
#include "virtqueue.h"

#include "device.h"

#include <string.h>

struct event {
	int16_t type;
	int16_t code;
	int32_t value;
} __attribute((scalar_storage_order("little-endian")));

typedef void (*GNEFilterType)(EventRecord *event, Boolean *result);

short funnel(long commandCode, void *pb);
static void handleEvent(struct event e);
static void reQueue(int bufnum);

static struct event *lpage;
static uint32_t ppage;
static volatile uint32_t retlens[4096 / sizeof (struct event)];

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

// Remember that this only needs to allow/deny the request, cleanup.c handles the rest
int DriverStop(void) {
	printf("Stopping\n");
	return noErr;
}

int DriverStart(short refNum) {
	InitLog();
	sprintf(LogPrefix, "Input(%d) ", refNum);

	if (!VInit(refNum)) {
		printf("Transport layer failure\n");
		goto openErr;
	};

	lpage = AllocPages(1, &ppage);
	if (lpage == NULL) {
		printf("Memory allocation failure\n");
		goto openErr;
	}
	RegisterCleanupVoidPtr(FreePages, lpage);

	if (!VFeaturesOK()) {
		printf("Feature negotiation failure\n");
		goto openErr;
	}

	VDriverOK();

	int nbuf = QInit(0, 4096 / sizeof (struct event));
	if (nbuf == 0) {
		printf("Virtqueue layer failure\n");
		goto openErr;
	}

	for (int i=0; i<nbuf; i++) {
		reQueue(i);
	}

	ScrollInit();

	printf("Ready\n");
	return noErr;
openErr:
	VFail();
	return openErr;
}

int DriverRead(IOParam *pb) {
	return readErr;
}

int DriverWrite(IOParam *pb) {
	return writErr;
}

int DriverCtl(CntrlParam *pb) {
	return controlErr;
}

int DriverStatus(CntrlParam *pb) {
	return statusErr;
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
	} else if (e.type == EV_REL && e.code == REL_WHEEL) {
		Scroll(e.value);
	} else if (e.type == EV_SYN) {
		if (knowpos) {
			// Scale to screen size (in lowmem globals)
			unsigned long realx = x * XLMGetRowBits() / 0x8000 + 1;
			unsigned long realy = y * XLMGetColLines() / 0x8000 + 1;

			LMSetMouseTemp((Point){realy, realx});
			LMSetRawMouseLocation((Point){realy, realx});

			LMSetCursorNew(XLMGetCrsrCouple());

			// Call JCrsrTask to redraw the cursor immediately.
			// Feels much more responsive than waiting for another interrupt.
			// Could a race condition garble the cursor? Haven't seen it happen.
			// if (*(char *)(0x174 + 7) & 1) // Uncomment to switch on shift key
			CALL0(void, XLMGetJCrsrTask());
		}

		knowpos = false;

		newbtn = (newbtn & knowmask) | (oldbtn & ~knowmask);

		if ((oldbtn != 0) != (newbtn != 0)) {
			LMSetMouseButtonState(newbtn ? 0 : 0x80);

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

static void reQueue(int bufnum) {
	QSend(0, 0/*n-send*/, 1/*n-recv*/,
		(uint32_t []){ppage + sizeof (struct event) * bufnum},
		(uint32_t []){sizeof (struct event)},
		&retlens[bufnum],
		false/*wait*/);
}

void DNotified(uint16_t q, volatile uint32_t *retlen) {
	int bufnum = retlen - retlens;
	handleEvent(lpage[bufnum]);
	reQueue(bufnum);
}

void DConfigChange(void) {
}
