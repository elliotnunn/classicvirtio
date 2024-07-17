/* Copyright (c) 2023-2024 Elliot Nunn */
/* Licensed under the MIT license */

// File System Manager functions (UT_*) to access the FCB table may be:
// unavailable and unnecessary (-7.1, or System file hasn't loaded yet)
// available but unnecessary (7.5-8.6)
// available and necessary (9.0-)
// And the accessors weren't in InterfaceLib until 8.5!
// So we must polyfill them.

// Since we are replacing the "UT_" functions, change the FCBRec struct
// to repurpose some fields for the multifork layer.

// And while we are at it, we do reserve FCB 0 for "fictional use"

#pragma once

#include <FSM.h>
#include <Types.h>
#include <stdint.h>

// Fields inside a union are available for multifork use
struct MyFCB {
	uint32_t fcbFlNm;            // FCB file number. Non-zero marks FCB used
	char fcbFlags;               // FCB flags
	char fcbTypByt;              // File type byte
	union {
		char pad1[2];
		short nextOfSameFile; // circular linked list owned by device-9p.c
	};
	uint32_t fcbEOF;             // Logical length or EOF in bytes
	uint32_t fcbPLen;            // Physical file length in bytes
	uint32_t fcbCrPs;            // Current position within file
	VCBPtr fcbVPtr;              // Pointer to the corresponding VCB
	void *fcbBfAdr;              // File's buffer address
	union {
		char pad2[2];
	};
	uint32_t fcbClmpSize;        // Number of bytes per clump
	void *fcbBTCBPtr;            // Pointer to B*-Tree control block for file
	union {
		char pad3[12];
	};
	OSType fcbFType;             // File's 4 Finder Type bytes
	union {
		char pad4[4];
	};
	uint32_t fcbDirID;           // Parent Directory ID
	Str31 fcbCName;              // CName of open file
} __attribute__((packed));

static char testStructSize1[94-sizeof (struct MyFCB)];
static char testStructSize2[sizeof (struct MyFCB)-94];

OSErr UnivAllocateFCB(short *fileRefNum, struct MyFCB **fileCtrlBlockPtr);
OSErr UnivResolveFCB(short fileRefNum, struct MyFCB **fileCtrlBlockPtr);
OSErr UnivIndexFCB(VCBPtr volCtrlBlockPtr, short *fileRefNum, struct MyFCB **fileCtrlBlockPtr);
