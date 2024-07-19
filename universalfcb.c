/* Copyright (c) 2023-2024 Elliot Nunn */
/* Licensed under the MIT license */

// A polyfill is needed for the FSM accessor functions (UT*)
// because they may be:
//   ROM-7.1 unavailable and unnecessary (incl when System file hasn't loaded yet)
//   7.5-8.1 available (trap only) but unnecessary
//   8.5-8.6 available (trap + InterfaceLib) but unnecessary
//   9.0-9.2 available and NECESSARY
// Details in TN1184 "FCBs, Now and Forever"

#include <FSM.h>
#if GENERATINGCFM
#include <MixedMode.h>
#else
#define CallUniversalProc(a, ...) 0
#endif
#include <stdbool.h>
#include <string.h>

#include "panic.h"

#include "universalfcb.h"

static int os9Format(void);
static void *fcbBase(void);
static bool refNumValid(short refNum);
unsigned int hash(uint32_t cnid, bool resfork);

static short lists[256]; // each a linked list of FCBs

struct MyFCB *UnivAllocateFile(void) {
	if (os9Format()) {
		short refNum;
		struct MyFCB *fcb;
		// pascal OSErr UTAllocateFCB(short *fileRefNum, FCBRecPtr *fileCtrlBlockPtr);
		short err = CallUniversalProc(*(UniversalProcPtr *)0xe90, 0xfe8, 0, &refNum, &fcb);
		if (err) {
			return NULL;
		} else {
			memset(fcb, 0, 94); // ?UTGetForkControlBlockSize
			fcb->refNum = refNum;
			return fcb;
		}
	} else {
		void *base = fcbBase();
		short len = *(short *)base;
		for (short refNum=2; refNum<len; refNum+=94) {
			struct MyFCB *fcb = base + refNum;
			if (fcb->fcbFlNm == 0) {
				memset(fcb, 0, 94);
				fcb->refNum = refNum;
				return fcb;
			}
		}
		return NULL;
	}
}

void UnivEnlistFile(struct MyFCB *fcb) {
	if (fcb->fcbFlNm==0 || fcb->refNum==0) {
		panic("UnivEnlistFile of zero FCB");
	}

	int key = hash(fcb->fcbFlNm, fcb->fcbFlags&fcbResourceMask);
	if (lists[key]) {
		struct MyFCB *left = UnivMustGetFCB(lists[key]);
		struct MyFCB *right = UnivMustGetFCB(left->right);
		fcb->right = left->right;
		fcb->left = right->left;
		left->right = fcb->refNum;
		right->left = fcb->refNum;
	} else {
		fcb->left = fcb->right = fcb->refNum;
		lists[key] = fcb->refNum;
	}
}

void UnivDelistFile(struct MyFCB *fcb) {
	if (fcb->fcbFlNm==0 || fcb->refNum==0) {
		panic("UnivDelistFile of zero FCB");
	}

	int key = hash(fcb->fcbFlNm, fcb->fcbFlags&fcbResourceMask);
	if (lists[key] == fcb->refNum) {
		if (fcb->left == fcb->refNum) {
			lists[key] = 0; // only element, then there were none
		} else {
			lists[key] = fcb->left;
		}
	}

	struct MyFCB *left = UnivMustGetFCB(fcb->left);
	struct MyFCB *right = UnivMustGetFCB(fcb->right);
	left->right = fcb->right;
	right->left = fcb->left;
}

struct MyFCB *UnivGetFCB(short refNum) {
	if (os9Format()) {
		struct MyFCB *fcb;
		// pascal OSErr UTResolveFCB(short fileRefNum, FCBRecPtr *fileCtrlBlockPtr);
		CallUniversalProc(*(UniversalProcPtr *)0xe90, 0xee8, 5, refNum, &fcb);
		return fcb;
	} else {
		if (!refNumValid(refNum)) {
			return NULL;
		}
		return fcbBase() + refNum;
	}
}

struct MyFCB *UnivMustGetFCB(short refNum) {
	struct MyFCB *ret = UnivGetFCB(refNum);
	if (ret == NULL) {
		panic("UnivMustGetFCB on bad refNum");
	}
	return ret;
}

struct MyFCB *UnivFirst(uint32_t cnid, bool resfork) {
	int key = hash(cnid, resfork);
	if (lists[key] == 0) {
		return NULL;
	}

	short search = lists[key];
	for (;;) {
		struct MyFCB *fcb = UnivMustGetFCB(search);
		if (fcb->fcbFlNm==cnid && !!(fcb->fcbFlags&fcbResourceMask)==resfork) {
			return fcb;
		}

		search = fcb->right;
		if (search == lists[key]) {
			return NULL; // only one in the list, and it doesn't match
		}
	}
}

struct MyFCB *UnivNext(struct MyFCB *fcb) {
	uint32_t cnid = fcb->fcbFlNm;
	bool resfork = fcb->fcbFlags&fcbResourceMask ;

	int key = hash(cnid, resfork);
	if (lists[key] == 0) {
		panic("UnivNext on unlisted FCB");
	}

	short search = lists[key];
	for (;;) {
		short search = fcb->right;
		fcb = UnivMustGetFCB(search);

		if (search == lists[key]) {
			return NULL; // circled to the start of the list
		}

		if (fcb->fcbFlNm==cnid && !!(fcb->fcbFlags&fcbResourceMask)==resfork) {
			return fcb;
		}
	}
}

// Is the Mac OS 9 FCB format in use?
// This can change at early boot, and is cheap to determine anyhow.
static int os9Format(void) {
#if GENERATINGCFM
	short fcbLen = *(short *)0x3f6;
	return fcbLen > 0 && fcbLen != 94;
#else
	return 0;
#endif
}

// only use if !os9Format()
static void *fcbBase(void) {
	unsigned short hi = *(unsigned short *)0x34e;
	unsigned short lo = *(unsigned short *)0x350;
	return (void *)(((unsigned long)hi << 16) | lo);
}

// only use if !os9Format()
static bool refNumValid(short refNum) {
	return refNum >= 2 &&
		refNum < *(short *)fcbBase() &&
		(refNum % *(short *)0x3f6) == 2;
}

// Which bucket (i.e. linked list)
unsigned int hash(uint32_t cnid, bool resfork) {
	return (cnid ^ resfork) % (sizeof lists/sizeof *lists);
}
