/* Copyright (c) Elliot Nunn */
/* Licensed under the MIT license */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <Devices.h>
#include <Files.h>
#include <FSM.h>

#include "constnames.h"
#include "scratch.h"
#include "traptest.h"

void testOpenPerms(void) {
	const struct {
		int mode, fcbFlags;
	} TA[] = {
		{fsCurPerm, fcbWriteMask},
		{fsRdPerm, 0},
		{fsWrPerm, fcbWriteMask},
		{fsRdWrPerm, fcbWriteMask},
		{fsRdWrShPerm, fcbWriteMask|fcbSharedWriteMask},
	};
	for (int i=0; i<sizeof TA/sizeof *TA; i++) {
		MkScratchTree("**D(F)");
		printf("# Checking the FCB flags that result from opening as %s\n", PermissionName(TA[i].mode));
		TrapTest(tOpen, ioNamePtr, "F", ioPermssn, TA[i].mode, END, ioResult, noErr, END);
		short refNum = GetField16(ioRefNum);
		TrapTest(tGetFCBInfo, ioVRefNum, "root", ioRefNum, refNum, END, ioResult, noErr, ioFCBFlags, TA[i].fcbFlags<<8, END);
	}

	const struct {
		int mode1, mode2, err, fcbFlags;
	} TB[] = {
		{fsRdPerm, fsCurPerm, noErr, fcbWriteMask},
		{fsRdPerm, fsRdPerm, noErr, 0},
		{fsRdPerm, fsWrPerm, noErr, fcbWriteMask},
		{fsRdPerm, fsRdWrPerm, noErr, fcbWriteMask},
		{fsRdPerm, fsRdWrShPerm, noErr, fcbWriteMask|fcbSharedWriteMask},

		{fsRdWrPerm, fsCurPerm, opWrErr},
		{fsRdWrPerm, fsRdPerm, noErr, 0},
		{fsRdWrPerm, fsWrPerm, opWrErr},
		{fsRdWrPerm, fsRdWrPerm, opWrErr},
		{fsRdWrPerm, fsRdWrShPerm, opWrErr},

		{fsRdWrShPerm, fsCurPerm, opWrErr},
		{fsRdWrShPerm, fsRdPerm, noErr, 0},
		{fsRdWrShPerm, fsWrPerm, opWrErr},
		{fsRdWrShPerm, fsRdWrPerm, opWrErr},
		{fsRdWrShPerm, fsRdWrShPerm, noErr, fcbWriteMask|fcbSharedWriteMask},
	};
	for (int i=0; i<sizeof TB/sizeof *TB; i++) {
		MkScratchTree("**D(F)");
		printf("# Checking the behavior of %s/%s double-open\n",
			PermissionName(TB[i].mode1),
			PermissionName(TB[i].mode2));
		TrapTest(tOpen, ioNamePtr, "F", ioPermssn, TB[i].mode1, END, ioResult, noErr, END);
		TrapTest(tOpen, ioNamePtr, "F", ioPermssn, TB[i].mode2, END, ioResult, TB[i].err, END);
		if (TB[i].err == noErr) {
			short refNum = GetField16(ioRefNum);
			TrapTest(tGetFCBInfo, ioVRefNum, "root", ioRefNum, refNum, END, ioResult, noErr, ioFCBFlags, TB[i].fcbFlags<<8, END);
		}
	}
}
