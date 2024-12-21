/* Copyright (c) 2023-2024 Elliot Nunn */
/* Licensed under the MIT license */

// Functions to access and manipulate Fork Control Blocks (FCBs)
//   Good for any ROM/System/FSM combination
//   More ergonomic than the File System Manager API (just returns the answer!)
//   FCBRec replaced with MyFCB to repurpose some fields for our own use
//   Open FCBs for a given file can be listed using a hash table:
//      for (struct MyFCB *fcb=UnivFirst(cnid, resfork); fcb!=NULL; fcb=UnivNext(fcb)) {}

#pragma once
#include <FSM.h>
#include <Types.h>
#include <stdint.h>

// Fields inside a union are repurposed and maybe available for multifork use
// Must make sure that the fields here match the real FCB fields
struct MyFCB {
	uint32_t fcbFlNm;            // FCB file number. Non-zero marks FCB used
	char fcbFlags;               // FCB flags
	char fcbTypByt;              // File type byte
	union {
		char pad1[2];
		short refNum; // redundant, for convenience
	};
	uint32_t fcbEOF;             // Logical length or EOF in bytes
	uint32_t fcbPLen;            // Physical file length in bytes
	uint32_t fcbCrPs;            // Current position within file
	VCBPtr fcbVPtr;              // Pointer to the corresponding VCB
	void *fcbBfAdr;              // File's buffer address
	union {
		char pad2[2];
		char mfFlags;
	};
	uint32_t fcbClmpSize;        // Number of bytes per clump
	void *fcbBTCBPtr;            // Pointer to B*-Tree control block for file
	union {
		char pad3[12];
	};
	OSType fcbFType;             // File's 4 Finder Type bytes
	union {
		char pad4[4];
		struct {short left, right;}; // doubly linked list
	};
	uint32_t fcbDirID;           // Parent Directory ID
	Str31 fcbCName;              // CName of open file
} __attribute__((packed));

struct MyFCB *UnivAllocateFile(void); // returns NULL if out of FCBs
void UnivEnlistFile(struct MyFCB *fcb); // panics on invalid arg
void UnivDelistFile(struct MyFCB *fcb); // panics on invalid arg
struct MyFCB *UnivGetFCB(short refnum); // returns NULL on invalid arg
struct MyFCB *UnivMustGetFCB(short refnum); // panics on invalid arg
struct MyFCB *UnivFirst(uint32_t cnid, bool resfork);
struct MyFCB *UnivNext(struct MyFCB *fcb);
void UnivCloseAll(void);

// Example loop:
// for (struct MyFCB *fcb=UnivFirst(cnid, resfork); fcb!=NULL; fcb=UnivNext(fcb)) {}
