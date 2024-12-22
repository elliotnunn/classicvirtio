/* Copyright (c) 2023 Elliot Nunn */
/* Licensed under the MIT license */

#include "printf.h"

#include <Devices.h>
#include <DriverSynchronization.h>
#include <OSUtils.h>
#include <Slots.h>

#include "callin68k.h"
#include "cleanup.h"
#include "device.h"
#include "panic.h"
#include "structs-mmio.h"
#include "virtqueue.h"

#include "transport.h"

struct goldfishPIC {
	uint32_t status; // count of pending interrupts
	uint32_t pending; // pending mask
	uint32_t disableAll; // only way to clear a rupt? or device can?
	uint32_t disable; // write bitmask
	uint32_t enable; // write bitmask
}; // platform native byte order

static long interrupt(void);
static long interruptCompleteStub(void);
static void cleanupIntHandler(void *handler);
static void whoami(short refNum);

// Globals declared in transport.h, define here
void *VConfig;
uint16_t VMaxQueues;

static int slot; // these three variables are set by whoami()
static volatile struct virtioMMIO *device;
static volatile struct goldfishPIC *pic;

// Coexists in a queue with all the Virtio devices on this same board
static struct SlotIntQElement slotInterrupt = {
	.sqType = sIQType,
	.sqPrio = 20, // do before the "backstop"
	.sqAddr = CALLIN68K_C_ARG0_GLOBDEF(interrupt),
};

// An extra interrupt handler that returns "1" to prevent a dsBadSlotInt
static struct SlotIntQElement slotInterruptBackstop = {
	.sqType = sIQType,
	.sqPrio = 10, // do last
	.sqAddr = CALLIN68K_C_ARG0_GLOBDEF(interruptCompleteStub),
};

// returns true for OK
bool VInit(short refNum) {
	whoami(refNum);
	VConfig = (void *)&device->config;

	// 1. Reset the device.
	VReset();

	// 2. Set the ACKNOWLEDGE status bit: the guest OS has noticed the device.
	device->status = 1;
	SynchronizeIO();
	RegisterCleanup(VReset);

	// 3. Set the DRIVER status bit: the guest OS knows how to drive the device.
	device->status = 1 | 2;
	SynchronizeIO();

	// Absolutely require the version 1 "non-legacy" spec
	if (!VGetDevFeature(32)) {
		VFail();
		return false;
	}
	VSetFeature(32, true);

	if (SIntInstall(&slotInterrupt, slot)) return false;
	RegisterCleanupVoidPtr(cleanupIntHandler, &slotInterrupt);
	if (SIntInstall(&slotInterruptBackstop, slot)) return false;
	RegisterCleanupVoidPtr(cleanupIntHandler, &slotInterruptBackstop);
	RegisterCleanup(VReset); // a spurious interrupt will bomb

	pic->enable = 0xffffffff; // enable all sources, don't discriminate
	SynchronizeIO();

	return true;
}

// Negotiate features
bool VGetDevFeature(uint32_t number) {
	SynchronizeIO();
	device->deviceFeaturesSel = number / 32;
	SynchronizeIO();
	return (device->deviceFeatures >> (number % 32)) & 1;
}

void VSetFeature(uint32_t number, bool val) {
	uint32_t mask = 1 << (number % 32);
	uint32_t bits;

	SynchronizeIO();
	device->driverFeaturesSel = number / 32;
	SynchronizeIO();

	bits = device->driverFeatures;
	bits = val ? (bits|mask) : (bits&~mask);
	device->driverFeatures = bits;
	SynchronizeIO();
}

bool VFeaturesOK(void) {
	SynchronizeIO();
	device->status = 1 | 2 | 8;
	SynchronizeIO();
	return (device->status & 8) != 0;
}

void VDriverOK(void) {
	SynchronizeIO();
	device->status = 1 | 2 | 4 | 8;
	SynchronizeIO();
}

void VFail(void) {
	if (device != NULL) {
		SynchronizeIO();
		device->status = 0x80;
		SynchronizeIO();
	}
}

void VReset(void) {
	SynchronizeIO();
	device->status = 0;
	SynchronizeIO();
	while (device->status) {} // wait till 0
}

// Tell the device where to find the three (split) virtqueue rings
uint16_t VQueueMaxSize(uint16_t q) {
	SynchronizeIO();
	device->queueSel = q;
	SynchronizeIO();
	return device->queueNumMax;
}

void VQueueSet(uint16_t q, uint16_t size, uint32_t desc, uint32_t avail, uint32_t used) {
	SynchronizeIO();
	device->queueSel = q;
	SynchronizeIO();
	device->queueNum = size;
	device->queueDesc = desc;
	device->queueDriver = avail;
	device->queueDevice = used;
	SynchronizeIO();
	device->queueReady = 1;
	SynchronizeIO();
}

// Tell the device about a change to the avail ring
void VNotify(uint16_t queue) {
	SynchronizeIO();
	device->queueNotify = queue;
	SynchronizeIO();
}

static long interrupt(void) {
	// Deassert the interrupt at the Virtio device level
	// (Don't poll the Goldfish)
	uint32_t flags = device->interruptStatus;
	if (flags) device->interruptACK = flags;

	if (flags & 1) {
		QNotified();
	}

	if (flags & 2) {
		DConfigChange();
	}

	// We have lowered the interrupt for this Virtio device.
	// Pretend we handled nothing, to continue down the handler chain.
	return 0; // "did not handle"
}

// Interrupt handler chain ends here
// Defensive: previously we tried to test for pending interrupts and return 1
// from interrupt, but we got occasional dsBadSlotInt crashes.
static long interruptCompleteStub(void) {
	return 1; // "did handle"
}

static void cleanupIntHandler(void *handler) {
	SIntRemove(handler, slot);
}

// This driver has been started by the system to drive a numbered sResource (128-254) of a numbered slot.
// Which differently-numbered virtio device (0-31) does this correspond to within the slot?
// Choose like this: determine that we are the Nth sResource of a particular type (Block, 9P...)
// and therefore choose the Nth virtio device of that type.
static void whoami(short refNum) {
	// first, slot number
	slot = 255 & (*(AuxDCEHandle)GetDCtlEntry(refNum))->dCtlSlot;
	int resNum = 255 & (*(AuxDCEHandle)GetDCtlEntry(refNum))->dCtlSlotId;

	pic = (struct goldfishPIC *)(0xf0000000UL + ((long)slot<<24)); // the 32 virtio devices share a goldfish

	struct SpBlock sp = {.spSlot=slot, .spID=resNum};
	SGetSRsrc(&sp);
	int type = 255 & sp.spDrvrHW; // the last field of "sRsrcType" in the declaration ROM

	// Is this the 1st Block sResource? The 4th?
	int nth = 0;
	for (int i=128; i<resNum; i++) {
		struct SpBlock sp = {.spSlot=slot, .spID=i};
		if (SGetSRsrc(&sp) == noErr && (255 & sp.spDrvrHW) == type) {
			nth++;
		}
	}

	// Then which of the 32 virtio devices does that correspond with?
	// Count device types (e.g. 3 of Block, 1 of Input)
	for (int i=31; i>=0; i--) { // reverse-iterate to match command line order
		device = (struct virtioMMIO *)(0xf0000000UL + ((long)slot<<24) + 0x200 + 0x200*i);

		// Spec says these three registers MUST be read in this order at init time
		if (device->magicValue != 0x74726976) continue;
		SynchronizeIO();
		if (device->version != 2) continue;
		SynchronizeIO();
		if (device->deviceID != type) continue;
		if (nth > 0) {
			nth--;
			continue;
		}
		return; // got it
	}
	SysError(0xd0d0);
}
