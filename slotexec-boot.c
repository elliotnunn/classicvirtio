/* Copyright (c) 2023 Elliot Nunn */
/* Licensed under the MIT license */

#include <LowMem.h>
#include <Slots.h>
#include <ROMDefs.h>
#include <Traps.h>
#include <Types.h>

#include <stdint.h>

void dbgStack(long) = {0xa9ff}; // for when MacsBug keyboard input is broken

void exec(struct SEBlock *pb) {
	if (pb->seBootState != 0 && pb->seBootState != 1) {
		pb->seStatus = -1; // unexpected
		return;
	}

	// We were selected as the PRAM boot device
	// OR late-boot opportunity to install other sResource drivers

	// sRsrcLoadRec gets false slot/srsrc id when seBootState == 1
	// so this is a workaround for now
	LMGetToolScratch()[0] = 'V';
	LMGetToolScratch()[1] = 'I';
	LMGetToolScratch()[2] = pb->seSlot;
	LMGetToolScratch()[3] = pb->sesRsrcId;

	struct SlotDevParam spb = {
		.ioNamePtr="\p.", // needs to start with dot, otherwise not important
		// but ioNamePtr can cause crashes if selected dynamically: why?
		.ioSPermssn=0,
		.ioSlot=pb->seSlot,
		.ioID=pb->sesRsrcId,
	};

	OSErr err = PBHOpenSync((void *)&spb);

	LMGetToolScratch()[0] = 0;

	pb->seRefNum = spb.ioSRefNum;
	pb->seStatus = err;
}
