/* Copyright (c) 2023 Elliot Nunn */
/* Licensed under the MIT license */

#include <FSM.h>
#include <MixedMode.h>

#include <stdbool.h>

#include "universalfcb.h"

// Is the Mac OS 9 FCB format in use?
// This can change at early boot, and is cheap to determine anyhow.
#if GENERATINGCFM
static int fcbFormat9(void) {
	short fcbLen = *(short *)0x3f6;
	return fcbLen > 0 && fcbLen != 94;
}
#else
#define fcbFormat9() 0
#define CallUniversalProc(a, ...) 0
#define CallUniversalProc(a, ...) 0
#endif

static void *fcbBase(void) {
	unsigned short hi = *(unsigned short *)0x34e;
	unsigned short lo = *(unsigned short *)0x350;
	return (void *)(((unsigned long)hi << 16) | lo);
}

static bool refNumValid(short refNum) {
	return refNum >= 2 &&
		refNum < *(short *)fcbBase() &&
		(refNum % *(short *)0x3f6) == 2;
}

OSErr UnivAllocateFCB(short *fileRefNum, struct MyFCB **fileCtrlBlockPtr) {
	if (fcbFormat9()) {
		return CallUniversalProc(*(UniversalProcPtr *)0xe90, 0xfe8, 0,
			fileRefNum, fileCtrlBlockPtr);
	} else {
		void *base = fcbBase();
		short len = *(short *)base;
		for (short refnum=2; refnum<len; refnum+=94) {
			if (*(long *)(base + refnum) == 0) {
				*fileRefNum = refnum;
				*fileCtrlBlockPtr = base + refnum;
				return noErr;
			}
		}
		return tmfoErr;
	}
}

OSErr UnivResolveFCB(short fileRefNum, struct MyFCB **fileCtrlBlockPtr) {
	if (fcbFormat9()) {
		return CallUniversalProc(*(UniversalProcPtr *)0xe90, 0xee8, 5,
			fileRefNum, fileCtrlBlockPtr);
	} else {
		if (!refNumValid(fileRefNum)) return paramErr;

		*fileCtrlBlockPtr = fcbBase() + fileRefNum;
		return noErr;
	}
}

OSErr UnivIndexFCB(VCBPtr volCtrlBlockPtr, short *fileRefNum, struct MyFCB **fileCtrlBlockPtr) {
	if (fcbFormat9()) {
		return CallUniversalProc(*(UniversalProcPtr *)0xe90, 0x3fe8, 4,
			volCtrlBlockPtr, fileRefNum, fileCtrlBlockPtr);
	} else {
		if (*fileRefNum == 0) {
			*fileRefNum = 2;
		} else {
			if (!refNumValid(*fileRefNum)) return paramErr;
			*fileRefNum += 94;
		}

		for (;;) {
			if (!refNumValid(*fileRefNum)) return fnfErr;

			*fileCtrlBlockPtr = fcbBase() + *fileRefNum;

			if (*(long *)(*fileCtrlBlockPtr) != 0) {
				if (volCtrlBlockPtr == NULL ||
					(*fileCtrlBlockPtr)->fcbVPtr == volCtrlBlockPtr
				) {
					return noErr;
				}
			}
			*fileRefNum += 94;
		}
	}
}
