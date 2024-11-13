/* Copyright (c) 2023 Elliot Nunn */
/* Licensed under the MIT license */

#include <Devices.h>
#include <DriverServices.h>
#include <Events.h>

#include "allocator.h"
#include "callout68k.h"
#include "printf.h"
#include "transport.h"
#include "scrollwheel.h"
#include "virtqueue.h"

#include "device.h"

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
static void reQueue(int bufnum);

// This is a neat trick, s.t. we can store the value--which is compile-time
// constant, but not a true const--without the compiler complaining about
// "folding a variable-length array to constant array as an extension." --peads
enum { MAX_SIZE = 4096 / sizeof(struct event) };
static struct event *lpage;
static uint32_t ppage;
static volatile uint32_t retlens[MAX_SIZE];

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

OSStatus DoDriverIO(AddressSpaceID spaceID, IOCommandID cmdID,
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

            switch (pb.pb->cntrlParam.csCode) {
                case goodbye:
                case killCode:
                    err = finalize(pb.finalInfo);
                    break;
                default:
                    err = controlErr;
                    break;
            }
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
        // Fixes complaint about the passing an `OSStatus` to a function taking
        // `OSErr`. It'd probably be optimized out, but it might be worth
        // considering a mask (i.e. 0xFFFF) on `err` as well. --peads
		return IOCommandIsComplete(cmdID, (OSErr) err);
	}
}

static OSStatus finalize(DriverFinalInfo *info) {

    SynchronizeIO();
    int nbuf = QFinal(info->refNum, MAX_SIZE);
    if (nbuf == 0) {
        printf("Virtqueue layer failure\n");
        VFail();
        return closErr;
    }
    SynchronizeIO();
    printf("Virtqueue layer finalized\n");

    SynchronizeIO();
    CloseDriver(info->refNum);
    SynchronizeIO();

    if (!VFinal(&info->deviceEntry)) {
        printf("Transport layer failure\n");
        VFail();
        return closErr;
    }
    printf("Transport layer finalized\n");

    SynchronizeIO();
    FreePages(&ppage);
    SynchronizeIO();
    printf("Removed Successfully\n");

    return noErr;
}

static OSStatus initialize(DriverInitInfo *info) {
    InitLog();
    sprintf(LogPrefix, "Input(%d) ", info->refNum);

    if (!VInit(&info->deviceEntry)) {
        printf("Transport layer failure\n");
        VFail();
        return openErr;
    }

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

    int nbuf = QInit(0, MAX_SIZE);
    if (nbuf == 0) {
        printf("Virtqueue layer failure\n");
        VFail();
        return openErr;
    }

    for (int i = 0; i < nbuf; i++) {
        reQueue(i);
    }

    ScrollInit();

    printf("Ready\n");
    return noErr;
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


    enum { MVFLAG = 1 };

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
        if (e.value)  newbtn |= 2;
    } else if (e.type == EV_REL && e.code == REL_WHEEL) {
        Scroll(e.value);
    } else if (e.type == EV_SYN) {
        if (knowpos) {
            // Scale to screen size (in lowmem globals)
            unsigned long realx = x * *(int16_t *) 0xc20 / 0x8000 + 1;
            unsigned long realy = y * *(int16_t *) 0xc22 / 0x8000 + 1;

            unsigned long point = (realy << 16) | realx;

            *(unsigned long *) 0x828 = point; // MTemp
            *(unsigned long *) 0x82c = point; // RawMouse

            *(char *) 0x8ce = *(char *) 0x8cf; // CrsrNew = CrsrCouple

            // Call JCrsrTask to redraw the cursor immediately.
            // Feels much more responsive than waiting for another interrupt.
            // Could a race condition garble the cursor? Haven't seen it happen.
            // if (*(char *)(0x174 + 7) & 1) // Uncomment to switch on shift key
            CALL0(void, *(void **) 0x8ee);
        }

        knowpos = false;

        newbtn = (newbtn & knowmask) | (oldbtn & ~knowmask);

        if ((oldbtn != 0) != (newbtn != 0)) {
            *(unsigned char *) 0x172 = newbtn ? 0 : 0x80;

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
            (uint32_t[]) {ppage + sizeof(struct event) * bufnum},
            (uint32_t[]) {sizeof(struct event)},
            &retlens[bufnum],
            false/*wait*/);
}

void DNotified( uint16_t q, volatile uint32_t *retlen) {
    int bufnum = retlen - retlens;
    handleEvent(lpage[bufnum]);
    reQueue(bufnum);
}

void DConfigChange(void) {
}
