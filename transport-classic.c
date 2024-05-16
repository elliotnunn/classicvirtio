/* Copyright (c) 2023 Elliot Nunn */
/* Licensed under the MIT license */

#include "printf.h"

#include <Devices.h>
#include <DriverSynchronization.h>
#include <OSUtils.h>
#include <Slots.h>

#include "callin68k.h"
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

static long interruptTopHalf(void);
static long interruptCompleteStub(void);
static void configIntBottomHalf(void);

// Globals declared in transport.h, define here
void *VConfig;
uint16_t VMaxQueues;

static volatile struct virtioMMIO *device;
static volatile struct goldfishPIC *pic;
static uint32_t picmask;

static bool qnotifying, cnotifying;

// Coexists in a queue with all the Virtio devices on this same board
static struct SlotIntQElement slotInterrupt = {
	.sqType = sIQType,
	.sqPrio = 20, // do before the "backstop"
	.sqAddr = CALLIN68K_C_ARG0_GLOBDEF(interruptTopHalf),
};

// An extra interrupt handler that returns "1" to prevent a dsBadSlotInt
static struct SlotIntQElement slotInterruptBackstop = {
	.sqType = sIQType,
	.sqPrio = 10, // do last
	.sqAddr = CALLIN68K_C_ARG0_GLOBDEF(interruptCompleteStub),
};

static struct DeferredTask queueDeferredTask = {
	.qType = dtQType,
	.dtAddr = CALLIN68K_C_ARG0_GLOBDEF(QNotified),
};

static struct DeferredTask configDeferredTask = {
	.qType = dtQType,
	.dtAddr = CALLIN68K_C_ARG0_GLOBDEF(configIntBottomHalf),
};

// returns true for OK
bool VInit(RegEntryID *id) {
	// Work around a shortcoming in global initialisation
	int slotnum = id->contents[0];
	int devindex = id->contents[1];

	picmask = 1 << devindex;

	pic = (void *)(0xf0000000 + 0x1000000*slotnum);
	device = (void *)(0xf0000000 + 0x1000000*slotnum + 0x200*(devindex+1));
	VConfig = (void *)(0xf0000000 + 0x1000000*slotnum + 0x200*(devindex+1) + 0x100);

	SynchronizeIO();
	if (device->magicValue != 0x74726976) return false;
	SynchronizeIO();
	if (device->version != 2) return false;
	SynchronizeIO();

	// MUST read deviceID and status in that order
	if (device->deviceID == 0) return false;
	SynchronizeIO();
	if (device->status != 0) return false;
	SynchronizeIO();

	// 1. Reset the device.
	device->status = 0;
	SynchronizeIO();
	while (device->status) {} // wait till 0

	// 2. Set the ACKNOWLEDGE status bit: the guest OS has noticed the device.
	device->status = 1;
	SynchronizeIO();

	// 3. Set the DRIVER status bit: the guest OS knows how to drive the device.
	device->status = 1 | 2;
	SynchronizeIO();

	// Absolutely require the version 1 "non-legacy" spec
	if (!VGetDevFeature(32)) {
		VFail();
		return false;
	}
	VSetFeature(32, true);

	if (SIntInstall(&slotInterrupt, slotnum)) return false;
	if (SIntInstall(&slotInterruptBackstop, slotnum)) return false;

	pic->enable = 0xffffffff;
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
	SynchronizeIO();
	device->status = 0x80;
	SynchronizeIO();
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

static long interruptTopHalf(void) {
	// Ignore the Goldfish for now, just check the Virtio registers for an
	// interrupt, and lower it if needed
	uint32_t flags = device->interruptStatus;
	SynchronizeIO();
	if (flags) device->interruptACK = flags;
	SynchronizeIO();

	if ((flags & 1) && !qnotifying) {
		QDisarm();
		qnotifying = true;
		DTInstall(&queueDeferredTask);
	}

	if ((flags & 2) && !cnotifying) {
		cnotifying = true;
		DTInstall(&configDeferredTask);
	}

	// We have lowered the interrupt for this Virtio device.
	// Pretend we handled nothing, to continue down the handler chain.
	return 0; // "did not handle"
}

// Interrupt handler chain ends here
// Defensive: previously we tried to test for pending interrupts and return 1
// from interruptTopHalf, but we got occasional dsBadSlotInt crashes.
static long interruptCompleteStub(void) {
	return 1; // "did handle"
}

static void configIntBottomHalf(void) {
	cnotifying = false;
	DConfigChange();
}

// Interrupts need to be explicitly reenabled after a notification
void VRearm(void) {
	qnotifying = false;
}
