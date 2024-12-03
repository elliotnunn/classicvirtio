/* Copyright (c) 2023 Elliot Nunn */
/* Licensed under the MIT license */

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

	struct SlotDevParam spb = {
		.ioNamePtr="\p.", // needs to start with dot, otherwise not important
		// but ioNamePtr can cause crashes if selected dynamically: why?
		.ioSPermssn=0,
		.ioSlot=pb->seSlot,
		.ioID=pb->sesRsrcId,
	};

	OSErr err = PBHOpenSync((void *)&spb);

	pb->seRefNum = spb.ioSRefNum;
	pb->seStatus = err;
}
