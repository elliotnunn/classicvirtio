#include <Slots.h>

#include <string.h>

#include "structs-mmio.h"

volatile char *virtioSerialRegister(void) {
	for (int slot=8; slot<16; slot++) {
		struct SpBlock sp = {.spSlot=slot, .spID=1/*board sResource*/};
		if (SRsrcInfo(&sp)) continue;
		sp.spID = 2/*sRsrcName*/;
		if (SFindStruct(&sp)) continue;
		if (memcmp(sp.spsPointer, "Virtio", 6)) continue;

		void *base = (void *)0xf0000000 + ((long)slot << 24);
		for (int dev=0; dev<32; dev++) {
			struct virtioMMIO *device = base + 0x200 + 0x200*dev;
			if (device->magicValue==0x74726976 && device->version==2 && device->deviceID==3) {
				return &device->config[8];
			}
		}
	}
	return NULL;
}
