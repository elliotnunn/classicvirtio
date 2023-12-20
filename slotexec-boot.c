/* Copyright (c) 2023 Elliot Nunn */
/* Licensed under the MIT license */

#include <Slots.h>
#include <ROMDefs.h>
#include <Traps.h>
#include <Types.h>

#include <stdint.h>

#include "printf.h"

void dbgStack(long) = {0xa9ff}; // for when MacsBug keyboard input is broken

void exec(struct SEBlock *pb) {
	if (pb->seBootState == 0) {
		// We were selected as the PRAM boot device
		struct SlotDevParam spb = {
			.ioNamePtr="\x09" ".Virtio9P",
			.ioSPermssn=0,
			.ioSlot=pb->seSlot,
			.ioID=pb->sesRsrcId,
		};

		OSErr err = PBHOpenSync((void *)&spb);

		pb->seRefNum = spb.ioSRefNum;
		pb->seStatus = err;
	} else if (pb->seBootState == 1) {
		// Late-boot opportunity to install other sResource drivers
		// (This seems to crash if there is an sRsrcLoadRec)
		struct SlotDevParam spb = {
			.ioNamePtr="\x09" ".Virtio9P",
			.ioSPermssn=0,
			.ioSlot=pb->seSlot,
			.ioID=pb->sesRsrcId,
		};

		OSErr err = PBHOpenSync((void *)&spb);

		pb->seStatus = 0; // seSuccess
	} else {
		pb->seStatus = -1; // unexpected
	}
}
