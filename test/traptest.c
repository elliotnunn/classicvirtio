/* Copyright (c) Elliot Nunn */
/* Licensed under the MIT license */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <Memory.h>
#include <TextUtils.h>

#include "constnames.h"
#include "scratch.h"
#include "tap.h"

#include "traptest.h"

static int trap(void *pb, uint16_t trap, uint16_t selector);
static int cpstrcmp(const char *c, const char *p);
static void printpb(void *pb, const char *prefix);

enum {
	NORMAL,
	STRING,
	CNID,
	VREFNUM,
	ERR,
};

struct field {
	uint8_t size, offset, special;
};

static const struct field fields[] = {
	[ioResult]       = { 2, 0x10, ERR},
	[ioFileName]     = { 4, 0x12, STRING},
	[ioNamePtr]      = { 4, 0x12, STRING},
	[ioVRefNum]      = { 2, 0x16, VREFNUM},
	[ioDstVRefNum]   = { 2, 0x18, VREFNUM},
	[ioFRefNum]      = { 2, 0x18},
	[ioRefNum]       = { 2, 0x18},
	[ioDenyModes]    = { 2, 0x1a},
	[ioFVersNum]     = { 1, 0x1a},
	[ioVersNum]      = { 1, 0x1a},
	[ioWDIndex]      = { 2, 0x1a},
	[ioPermssn]      = { 1, 0x1b},
	[ioFCBIndx]      = { 4, 0x1c},
	[ioFDirIndex]    = { 2, 0x1c},
	[ioNewName]      = { 4, 0x1c, STRING},
	[ioVolIndex]     = { 2, 0x1c},
	[ioWDProcID]     = { 4, 0x1c},
	[ioFlAttrib]     = { 1, 0x1e},
	[ioVCrDate]      = { 4, 0x1e},
	[ioFlVersNum]    = { 1, 0x1f},
	[ioBuffer]       = { 4, 0x20},
	[ioCopyName]     = { 4, 0x20, STRING},
	[ioDrUsrWds]     = {16, 0x20},
	[ioFCBFlNm]      = { 4, 0x20},
	[ioFlFndrInfo]   = {16, 0x20},
	[ioFlags]        = { 2, 0x20},
	[ioWDVRefNum]    = { 2, 0x20, VREFNUM},
	[ioSlot]         = { 1, 0x22},
	[ioVLsBkUp]      = { 4, 0x22},
	[ioVLsMod]       = { 4, 0x22},
	[ioFCBFlags]     = { 2, 0x24},
	[ioNewDirID]     = { 4, 0x24, CNID},
	[ioReqCount]     = { 4, 0x24},
	[ioFCBStBlk]     = { 2, 0x26},
	[ioVAtrb]        = { 2, 0x26},
	[ioActCount]     = { 4, 0x28},
	[ioFCBEOF]       = { 4, 0x28},
	[ioVNmFls]       = { 2, 0x28},
	[ioVBitMap]      = { 2, 0x2a},
	[ioVDirSt]       = { 2, 0x2a},
	[ioFCBPLen]      = { 4, 0x2c},
	[ioPosMode]      = { 2, 0x2c},
	[ioVAllocPtr]    = { 2, 0x2c},
	[ioVBlLn]        = { 2, 0x2c},
	[ioPosOffset]    = { 4, 0x2e},
	[ioVNmAlBlks]    = { 2, 0x2e},
	[ioDirID]        = { 4, 0x30, CNID},
	[ioDrDirID]      = { 4, 0x30, CNID},
	[ioFCBCrPs]      = { 4, 0x30},
	[ioFlNum]        = { 4, 0x30},
	[ioVAlBlkSiz]    = { 4, 0x30},
	[ioWDDirID]      = { 4, 0x30, CNID},
	[ioDrNmFls]      = { 2, 0x34},
	[ioFCBVRefNum]   = { 2, 0x34, VREFNUM},
	[ioFlStBlk]      = { 2, 0x34},
	[ioVClpSiz]      = { 4, 0x34},
	[ioFCBClpSiz]    = { 4, 0x36},
	[ioFileID]       = { 4, 0x36, CNID},
	[ioFlLgLen]      = { 4, 0x36},
	[ioAlBlSt]       = { 2, 0x38},
	[ioFCBParID]     = { 4, 0x3a, CNID},
	[ioFlPyLen]      = { 4, 0x3a},
	[ioVNxtFNum]     = { 4, 0x3a},
	[ioFlRStBlk]     = { 2, 0x3e},
	[ioVFrBlk]       = { 2, 0x3e},
	[ioFlRLgLen]     = { 4, 0x40},
	[ioVSigWord]     = { 2, 0x40},
	[ioVDrvInfo]     = { 2, 0x42},
	[ioFlRPyLen]     = { 4, 0x44},
	[ioVDRefNum]     = { 2, 0x44},
	[ioVFSID]        = { 2, 0x46},
	[ioDrCrDat]      = { 4, 0x48},
	[ioFlCrDat]      = { 4, 0x48},
	[ioVBkUp]        = { 4, 0x48},
	[ioDrMdDat]      = { 4, 0x4c},
	[ioFlMdDat]      = { 4, 0x4c},
	[ioVSeqNum]      = { 2, 0x4c},
	[ioVWrCnt]       = { 4, 0x4e},
	[ioDrBkDat]      = { 4, 0x50},
	[ioFlBkDat]      = { 4, 0x50},
	[ioVFilCnt]      = { 4, 0x52},
	[ioDrFndrInfo]   = {16, 0x54},
	[ioFlXFndrInfo]  = {16, 0x54},
	[ioVDirCnt]      = { 4, 0x56},
	[ioVFndrInfo]    = {32, 0x5a},
	[ioDrParID]      = { 4, 0x64, CNID},
	[ioFlParID]      = { 4, 0x64, CNID},
	[ioFlClpSiz]     = { 4, 0x68},
};

// icky machine code generation, but hard to do it any other way
static int trap(void *pb, uint16_t trap, uint16_t selector) {
	static uint16_t editcode[] = {
		0x206f, 0x0004, // move.l 4(sp),a0
		0x302f, 0x0008, // move.w 8(sp),d0
		0xa000,         // the actual trap at editcode[4]
		0x205f,         // move.l (sp)+,a0
		0x5c4f,         // addq  #6,sp
		0x4ed0,         // jmp (a0)
	};

	if (trap != editcode[4]) {
		editcode[4] = trap;
		BlockMove(editcode, editcode, sizeof editcode); // clear i-cache
	}

	typedef int16_t (*callptr)(void *pb, short selector);
	return ((callptr)editcode)(pb, selector);
}

static void printpb(void *pb, const char *prefix) {
	for (int i=0; i<128; i++) {
		if (i%16 == 0) {
			fputs(prefix, stdout);
		}
		printf("%02x", 255 & ((char *)pb)[i]);
		if (i%16 == 15) {
			putc('\n', stdout);
		} else if (i%2 == 1) {
			putc(' ', stdout);
		}
	}
}

// TrapTest(_GetVol, ioNamePtr, "something", NULL, ioResult, 0, NULL); // need to extend for return values
void TrapTest(uint32_t trapnum, ...) {
	char pb[128] __attribute__((aligned(4))) = {};
	char strings[1024];
	int strbytes = 0;

	// Populate parameter block
	va_list va;
	va_start(va, trapnum);
	char report[1024] = "";
	char *rep = report;
	rep = stpcpy(rep, TrapName(trapnum));
	*rep++ = '(';
	for (;;) {
		int field = va_arg(va, int);
		if (field == END) break;
		struct field f = fields[field];
		if (rep[-1] != '(') *rep++ = ' ';
		rep = stpcpy(rep, FieldName(field));

		int32_t setto = 0;
		if (f.special == NORMAL) {
			if (f.size <= sizeof (int)) {
				setto = va_arg(va, int); // ever actually happen?
			} else {
				setto = va_arg(va, long);
			}
			rep += sprintf(rep, "=%d", setto);
		} else if (f.special == STRING) {
			char *s = va_arg(va, char *);
			c2pstrcpy(strings+strbytes, s);
			SubVolName(strings+strbytes);
			setto = (int32_t)(strings+strbytes);
			strbytes += 256;
			rep += sprintf(rep, "=\"%s\"", s);
		} else if (f.special == CNID) {
			char *s = va_arg(va, char *);
			setto = ScratchDirID(s);
			rep += sprintf(rep, "=CNID(%s)", s);
		} else if (f.special == VREFNUM) {
			char *s = va_arg(va, char *);
			setto = ScratchWD(s);
			rep += sprintf(rep, "=WD(%s)", s);
		} else {
			TAPBailOut("bad code");
		}

		memcpy(pb+f.offset, (char *)&setto+4-f.size, f.size); // big endian field set
	}
	rep = stpcpy(rep, ") = (");

	// Call the trap synchronously
	// printpb(pb, "#  -> ");
	trap(pb, (uint16_t)(trapnum>>16), (uint16_t)trapnum);
	// printpb(pb, "# <-  ");

	// Check the parameter block against expected values
	char complaints[1024] = "";
	char *com = complaints;
	int bad = 0;
	for (;;) {
		int field = va_arg(va, int);
		if (field == END) break;
		struct field f = fields[field];
		if (rep[-1] != '(') *rep++ = ' ';
		*com++ = ' ';
		rep = stpcpy(rep, FieldName(field));
		com = stpcpy(com, FieldName(field));

		int32_t got = 0; // read the field from the PB (could be a string pointer)
		for (int i=0; i<f.size; i++) {
			got = (uint32_t)got << 8; // avoid undefined bitshift
			got |= (uint8_t)pb[f.offset+i];
		}
		uint32_t signbit = 1ULL<<(f.size*8-1);
		got = ((uint32_t)got^signbit) - signbit; // sign-extend

		if (f.special == NORMAL) {
			int32_t want;
			if (f.size <= sizeof (int)) {
				want = va_arg(va, int);
			} else {
				want = va_arg(va, long);
			}
			rep += sprintf(rep, "=%d", want);
			com += sprintf(com, "=%d", got);
			bad += (got != want);
		} else if (f.special == ERR) {
			int32_t want = va_arg(va, int);
			rep += sprintf(rep, "=%s", ErrName(want));
			com += sprintf(com, "=%s", ErrName(got));
			bad += (got != want);
		} else if (f.special == STRING) {
			char *got = (char *)got; // p string
			char *want = va_arg(va, char *); // c string
			unsigned char reallywant[256];
			c2pstrcpy(reallywant, want);
			SubVolName(reallywant);
			rep += sprintf(rep, "=\"%s\"", want);
			com += sprintf(com, "=\"%.*s\"", *got, got+1);
			bad += (memcmp(got, reallywant, 1+got[0]) != 0);
		} else if (f.special == CNID) {
			char *s = va_arg(va, char *);
			int32_t want = ScratchDirID(s);
			rep += sprintf(rep, "=CNID(%s)", s);
			com += sprintf(com, "=CNID(%s)", ScratchNameOfDirID(got));
			bad += (got != want);
		} else if (f.special == VREFNUM) {
			char *s = va_arg(va, char *);
			int32_t want = ScratchWD(s);
			rep += sprintf(rep, "=WD(%s)", s);
			com += sprintf(com, "=WD(%s)", ScratchNameOfWD(got));
			bad += (got != want);
		} else {
			TAPBailOut("bad code");
		}
	}
	rep = stpcpy(rep, ")"); // must null-terminate the report

	TAPResult(!bad, "%s", report);
	if (bad) {
		printf("# actually got: (%s)\n", complaints+1);
	}
	va_end(va);
}
