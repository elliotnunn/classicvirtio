/* Copyright (c) 2023 Elliot Nunn */
/* Licensed under the MIT license */

// Very few Toolbox calls allowed at Primary Init time

#include <DriverSynchronization.h>
#include <Slots.h>

#include <stdint.h>

#include "structs-mmio.h"

// Our job is to delete sResources that are excess to the 32 virtio devices we actually have
void exec(struct SEBlock *pb) {
	void *base = (void *)0xf0000000 + ((long)pb->seSlot << 24);

	char typeCounts[256] = {};

	// Count device types (e.g. 3 of Block, 1 of Input)
	for (int i=0; i<32; i++) {
		struct virtioMMIO *device = base + 0x200 + 0x200*i;

		if (device->magicValue != 0x74726976) continue;
		SynchronizeIO();
		if (device->version != 2) continue;
		SynchronizeIO();
		if (device->deviceID > 255) continue;
		typeCounts[device->deviceID]++;
	}

	// Delete unneeded functional sResources (e.g. there are 5 of Block so delete 2)
	for (int i=128; i<255; i++) {
		struct SpBlock sp = {.spSlot=pb->seSlot, .spID=i};
		if (SGetSRsrc(&sp)) continue; // not an sResource
		if ((sp.spDrvrHW & 0xff00) != 0x5600) continue; // not a virtio sResource
		int type = 255 & sp.spDrvrHW;

		if (typeCounts[type] == 0) {
			SDeleteSRTRec(&sp);
		} else {
			typeCounts[type]--;
		}
	}

	pb->seStatus = noErr;
}
