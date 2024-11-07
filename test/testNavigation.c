/* Copyright (c) Elliot Nunn */
/* Licensed under the MIT license */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <Devices.h>
#include <Files.h>

#include "constnames.h"
#include "scratch.h"
#include "traptest.h"

void testNavigation(void) {
	MkScratchTree("**D1(D2(F))");
	printf("# Testing GetCatInfo in vRefNum+dirID mode\n");
	TrapTest(tGetCatInfo, ioFDirIndex, -1, ioVRefNum, "zero", ioDirID, "zero", END, ioResult, noErr,  ioDrDirID, "D1",   ioDrParID, "root", END);
	TrapTest(tGetCatInfo, ioFDirIndex, -1, ioVRefNum, "zero", ioDirID, "root", END, ioResult, noErr,  ioDrDirID, "root", ioDrParID, "uber", END);
	TrapTest(tGetCatInfo, ioFDirIndex, -1, ioVRefNum, "zero", ioDirID, "fake", END, ioResult, fnfErr, END);
	TrapTest(tGetCatInfo, ioFDirIndex, -1, ioVRefNum, "zero", ioDirID, "D1",   END, ioResult, noErr,  ioDrDirID, "D1",   ioDrParID, "root", END);
	TrapTest(tGetCatInfo, ioFDirIndex, -1, ioVRefNum, "root", ioDirID, "root", END, ioResult, noErr,  ioDrDirID, "root", ioDrParID, "uber", END);
	TrapTest(tGetCatInfo, ioFDirIndex, -1, ioVRefNum, "root", ioDirID, "fake", END, ioResult, fnfErr, END);
	TrapTest(tGetCatInfo, ioFDirIndex, -1, ioVRefNum, "root", ioDirID, "D1",   END, ioResult, noErr,  ioDrDirID, "D1",   ioDrParID, "root", END);
	TrapTest(tGetCatInfo, ioFDirIndex, -1, ioVRefNum, "fake", ioDirID, "root", END, ioResult, nsvErr, END);
	TrapTest(tGetCatInfo, ioFDirIndex, -1, ioVRefNum, "D1",   ioDirID, "zero", END, ioResult, noErr,  ioDrDirID, "D1",   ioDrParID, "root", END);
	TrapTest(tGetCatInfo, ioFDirIndex, -1, ioVRefNum, "D1",   ioDirID, "root", END, ioResult, noErr,  ioDrDirID, "root", ioDrParID, "uber", END);
	TrapTest(tGetCatInfo, ioFDirIndex, -1, ioVRefNum, "D1",   ioDirID, "fake", END, ioResult, fnfErr, END);
	TrapTest(tGetCatInfo, ioFDirIndex, -1, ioVRefNum, "D1",   ioDirID, "D1",   END, ioResult, noErr,  ioDrDirID, "D1",   ioDrParID, "root", END);
	TrapTest(tGetCatInfo, ioFDirIndex, -1, ioVRefNum, "D1",   ioDirID, "D2",   END, ioResult, noErr,  ioDrDirID, "D2",   ioDrParID, "D1",   END);

	printf("# Testing GetCatInfo in vRefNum+dirID mode to access file (not permitted)\n");
	TrapTest(tGetCatInfo, ioFDirIndex, -1, ioVRefNum, "root", ioDirID, "F", END, ioResult, fnfErr, END);

	printf("# Testing GetCatInfo in vRefNum+dirID+string mode to access root (ioDrParID seems to be wrong for these)\n");
	TrapTest(tGetCatInfo, ioFDirIndex, 0, ioVRefNum, "root", ioDirID, "uber", ioNamePtr, "%", END, ioResult, noErr, ioDrDirID, "root", END);
	TrapTest(tGetCatInfo, ioFDirIndex, 0, ioVRefNum, "root", ioDirID, "uber", ioNamePtr, ":%:", END, ioResult, noErr, ioDrDirID, "root", END);
	TrapTest(tGetCatInfo, ioFDirIndex, 0, ioVRefNum, "zero", ioDirID, "uber", ioNamePtr, "%", END, ioResult, noErr, ioDrDirID, "root", END);
	TrapTest(tGetCatInfo, ioFDirIndex, 0, ioVRefNum, "D1", ioDirID, "uber", ioNamePtr, "%", END, ioResult, noErr, ioDrDirID, "root", END);
	TrapTest(tGetCatInfo, ioFDirIndex, 0, ioVRefNum, "fake", ioDirID, "fake", ioNamePtr, "%:", END, ioResult, noErr, ioDrDirID, "root", END);
	TrapTest(tGetCatInfo, ioFDirIndex, 0, ioVRefNum, "fake", ioDirID, "fake", ioNamePtr, "%:D1::", END, ioResult, noErr, ioDrDirID, "root", END);
	TrapTest(tGetCatInfo, ioFDirIndex, 0, ioVRefNum, "fake", ioDirID, "fake", ioNamePtr, "%:D1:D2:F::::", END, ioResult, dirNFErr, END);
	TrapTest(tGetCatInfo, ioFDirIndex, 0, ioVRefNum, "fake", ioDirID, "fake", ioNamePtr, "%:D1:D2:F::::nonexist", END, ioResult, dirNFErr, END);
	TrapTest(tGetCatInfo, ioFDirIndex, 0, ioVRefNum, "fake", ioDirID, "fake", ioNamePtr, "%:nonexist", END, ioResult, fnfErr, END);
	TrapTest(tGetCatInfo, ioFDirIndex, 0, ioVRefNum, "fake", ioDirID, "fake", ioNamePtr, "%:nonexist:", END, ioResult, fnfErr, END);
	TrapTest(tGetCatInfo, ioFDirIndex, 0, ioVRefNum, "fake", ioDirID, "fake", ioNamePtr, "%:nonexist::", END, ioResult, dirNFErr, END);
	TrapTest(tGetCatInfo, ioFDirIndex, 0, ioVRefNum, "fake", ioDirID, "fake", ioNamePtr, "%:nonexist:nonexist", END, ioResult, dirNFErr, END);

	printf("# Testing GetCatInfo in vRefNum+dirID+string mode to access file (ioDrParID seems to be wrong for these)\n");
	TrapTest(tGetCatInfo, ioFDirIndex, 0, ioVRefNum, "root", ioDirID, "F", ioNamePtr, "", END, ioResult, fnfErr, END);
	TrapTest(tGetCatInfo, ioFDirIndex, 0, ioVRefNum, "root", ioDirID, "uber", ioNamePtr, "%:D1:D2:F", END, ioResult, noErr, ioDirID, "F", END);
	TrapTest(tGetCatInfo, ioFDirIndex, 0, ioVRefNum, "root", ioDirID, "uber", ioNamePtr, ":%:D1:D2:F", END, ioResult, noErr, ioDirID, "F", END);
	TrapTest(tGetCatInfo, ioFDirIndex, 0, ioVRefNum, "root", ioDirID, "root", ioNamePtr, "%:D1:D2:F:", END, ioResult, noErr, ioDirID, "F", END);
	TrapTest(tGetCatInfo, ioFDirIndex, 0, ioVRefNum, "root", ioDirID, "root", ioNamePtr, "%:D1:D2:F", END, ioResult, noErr, ioDirID, "F", END);
	TrapTest(tGetCatInfo, ioFDirIndex, 0, ioVRefNum, "root", ioDirID, "root", ioNamePtr, "::%:D1:D2:F", END, ioResult, dirNFErr, END);
	TrapTest(tGetCatInfo, ioFDirIndex, 0, ioVRefNum, "zero", ioDirID, "zero", ioNamePtr, "%:D1:D2:F", END, ioResult, noErr, ioDirID, "F", END);
	TrapTest(tGetCatInfo, ioFDirIndex, 0, ioVRefNum, "zero", ioDirID, "zero", ioNamePtr, "%:D1:D2:F::F", END, ioResult, dirNFErr, END);
	TrapTest(tGetCatInfo, ioFDirIndex, 0, ioVRefNum, "zero", ioDirID, "zero", ioNamePtr, ":D2:F", END, ioResult, noErr, ioDirID, "F", END);
}
