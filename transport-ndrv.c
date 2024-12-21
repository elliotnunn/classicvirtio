/* Copyright (c) 2023 Elliot Nunn */
/* Licensed under the MIT license */

#include <stddef.h>
#include <stdint.h>

#include <Devices.h>
#include <DriverServices.h>
#include <NameRegistry.h>
#include <PCI.h>

#include "allocator.h"
#include "cleanup.h"
#include "device.h"
#include "structs-pci.h"
#include "virtqueue.h"

#include <stdbool.h>

#include "transport.h"

// Globals declared in transport.h, define here
void *VConfig;
uint16_t VMaxQueues;

// Internal globals
static volatile struct virtio_pci_common_cfg *gCommonConfig;
static uint16_t *gNotify;
static uint32_t gNotifyMultiplier;
static uint8_t *gISRStatus;
static RegEntryID dev;

// Internal routines
static void installInterrupt(void);
static void removeInterrupt(void);
static InterruptMemberNumber interrupt(InterruptSetMember ist, void *refCon, uint32_t intCount);
static void findLogicalBARs(RegEntryID *pciDevice, void *barArray[6]);

// Leave the device in DRIVER status.
bool VInit(short refNum) {
	GetDriverInformation(refNum,
		(UnitNumber []){0},             // junk
		(DriverFlags []){0},            // junk
		(DriverOpenCount []){0},        // junk
		(Str255){},                     // junk
		&dev,                           // return value of interest
		&(CFragSystem7Locator){.u={.onDisk={.fileSpec=&(FSSpec){}}}}, // junk that needs valid ptr
		(CFragConnectionID []){0},      // junk
		(DriverEntryPointPtr []){NULL}, // junk
		(DriverDescription []){{}});    // junk

	void *bars[6];
	findLogicalBARs(&dev, bars);

	// PCI configuration structures point to addresses we need within the BARs
	uint8_t cap_offset;
	for (ExpMgrConfigReadByte(&dev, (LogicalAddress)0x34, &cap_offset);
		cap_offset != 0;
		ExpMgrConfigReadByte(&dev, (LogicalAddress)(cap_offset+1), &cap_offset)) {

		uint8_t cap_vndr, cfg_type, bar;
		uint32_t offset;
		void *address;

		// vendor-specific capability struct, i.e. a "VIRTIO_*" one
		ExpMgrConfigReadByte(&dev, (LogicalAddress)(uint32_t)cap_offset, &cap_vndr);
		if (cap_vndr != 9) continue;

		ExpMgrConfigReadByte(&dev, (LogicalAddress)(cap_offset+3), &cfg_type);
		ExpMgrConfigReadByte(&dev, (LogicalAddress)(cap_offset+4), &bar);
		ExpMgrConfigReadLong(&dev, (LogicalAddress)(cap_offset+8), &offset);
		address = (char *)bars[bar] + offset;

		if (cfg_type == 1 && !gCommonConfig) {
			gCommonConfig = address;
		} else if (cfg_type == 2 && !gNotify) {
			gNotify = address;
			ExpMgrConfigReadLong(&dev, (LogicalAddress)(cap_offset+16), &gNotifyMultiplier);
		} else if (cfg_type == 3 && !gISRStatus) {
			gISRStatus = address;
		} else if (cfg_type == 4 && !VConfig) {
			VConfig = address;
		}
	}

	if (!gCommonConfig || !gNotify || !gISRStatus || !VConfig) return false;

	// Incantation to enable memory-mapped access
	uint16_t pci_status = 0;
	ExpMgrConfigReadWord(&dev, (LogicalAddress)4, &pci_status);
	pci_status |= 2;
	ExpMgrConfigWriteWord(&dev, (LogicalAddress)4, pci_status);

	VMaxQueues = gCommonConfig->num_queues;

	// 1. Reset the device.
	VReset();

	// 2. Set the ACKNOWLEDGE status bit: the guest OS has noticed the device.
	gCommonConfig->device_status = 1;
	SynchronizeIO();
	RegisterCleanup(VReset);

	// 3. Set the DRIVER status bit: the guest OS knows how to drive the device.
	gCommonConfig->device_status = 1 | 2;
	SynchronizeIO();

	// Absolutely require the version 1 "non-legacy" spec
	if (!VGetDevFeature(32)) {
		VFail();
		return false;
	}
	VSetFeature(32, true);

	// Install interrupt handler
	installInterrupt();
	RegisterCleanup(removeInterrupt);
	RegisterCleanup(VReset); // a spurious interrupt might bomb the system

	return true;
}

struct InterruptSetMember intTreeSpec; // to restore later...
static void *oldIntRefCon;
static InterruptHandler oldIntHandler;
static InterruptDisabler intDisabler;
static void installInterrupt(void) { // I seriously don't understand this code
	InterruptEnabler enabler;
	RegPropertyValueSize size = sizeof(intTreeSpec);

	RegistryPropertyGet(&dev, "driver-ist", (void *)&intTreeSpec, &size);
	GetInterruptFunctions(intTreeSpec.setID, intTreeSpec.member, &oldIntRefCon/*to restore*/, &oldIntHandler/*to restore*/, &enabler, &intDisabler);
	InstallInterruptFunctions(intTreeSpec.setID, intTreeSpec.member, NULL/*refcon*/, interrupt, NULL/*enabler=no change*/, NULL/*disabler=no change*/);

	enabler(intTreeSpec, oldIntRefCon);
}

static void removeInterrupt(void) {
	intDisabler(intTreeSpec, oldIntRefCon);
	InstallInterruptFunctions(intTreeSpec.setID, intTreeSpec.member, oldIntRefCon, oldIntHandler, NULL, NULL);
}

bool VGetDevFeature(uint32_t number) {
	gCommonConfig->device_feature_select = number / 32;
	SynchronizeIO();

	return (gCommonConfig->device_feature >> (number % 32)) & 1;
}

void VSetFeature(uint32_t number, bool val) {
	uint32_t mask = 1 << (number % 32);
	uint32_t bits;

	gCommonConfig->driver_feature_select = number / 32;
	SynchronizeIO();

	bits = gCommonConfig->driver_feature;
	bits = val ? (bits|mask) : (bits&~mask);
	gCommonConfig->driver_feature = bits;

	SynchronizeIO();
}

bool VFeaturesOK(void) {
	gCommonConfig->device_status = 1 | 2 | 8;
	SynchronizeIO();

	return (gCommonConfig->device_status & 8) != 0;
}

void VDriverOK(void) {
	gCommonConfig->device_status = 1 | 2 | 4 | 8;
	SynchronizeIO();
}

void VReset(void) {
	SynchronizeIO();
	gCommonConfig->device_status = 0;
	SynchronizeIO();
	while (gCommonConfig->device_status) {} // wait till 0
}

void VFail(void) {
	if (gCommonConfig != NULL) {
		SynchronizeIO();
		gCommonConfig->device_status = 0x80;
		SynchronizeIO();
	}
}

uint16_t VQueueMaxSize(uint16_t q) {
	gCommonConfig->queue_select = q;
	SynchronizeIO();
	return gCommonConfig->queue_size;
}

void VQueueSet(uint16_t q, uint16_t size, uint32_t desc, uint32_t avail, uint32_t used) {
	gCommonConfig->queue_select = q;
	SynchronizeIO();
	gCommonConfig->queue_size = size;
	gCommonConfig->queue_desc = desc;
	gCommonConfig->queue_driver = avail;
	gCommonConfig->queue_device = used;
	SynchronizeIO();
	gCommonConfig->queue_enable = 1;
	SynchronizeIO();
}

void VNotify(uint16_t queue) {
	gNotify[gNotifyMultiplier * queue / 2] = queue;
	SynchronizeIO();
}

static InterruptMemberNumber interrupt(InterruptSetMember ist, void *refCon, uint32_t intCount) {
	uint8_t flags = *gISRStatus; // read flags and also deassert the interrupt

	if (flags & 1) {
		QNotified();
	}

	if (flags & 2) {
		DConfigChange();
	}

	if (flags & 3) {
		return kIsrIsComplete;
	} else {
		return kIsrIsNotComplete;
	}
}

// Open Firmware and Mac OS have already assigned and mapped the BARs
// Just need to find where
static void findLogicalBARs(RegEntryID *pciDevice, void *barArray[6]) {
	#define MAXADDRS 10
	uint32_t assignAddrs[5*MAXADDRS] = {0};
	void *applAddrs[MAXADDRS] = {0};

	for (int i=0; i<6; i++) barArray[i] = NULL;

	RegPropertyValueSize size = sizeof(assignAddrs);
	RegistryPropertyGet(pciDevice, "assigned-addresses", (void *)assignAddrs, &size);

	size = sizeof(applAddrs);
	RegistryPropertyGet(pciDevice, "AAPL,address", applAddrs, &size);

	for (int i=0; i<MAXADDRS; i++) {
		uint8_t bar;

		// Only interested in PCI 32 or 64-bit memory space
		if (((assignAddrs[i*5] >> 24) & 3) < 2) continue;

		// This is the offset of a BAR within PCI config space (0x10, 0x14...)
		bar = assignAddrs[i*5];

		// Convert to a BAR number (0-5)
		if (bar % 4) continue;
		bar = (bar - 0x10) / 4;
		if (bar > 5) continue;

		// The base logical address is the i'th element of AAPL,address
		barArray[bar] = applAddrs[i];
	}
}
