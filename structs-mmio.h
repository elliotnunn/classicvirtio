#pragma once

#include <stdint.h>

struct virtioMMIO {
	uint32_t magicValue;        // 0x00 RO = 0x74726976 "virt"
	uint32_t version;           // 0x04 RO = 2
	uint32_t deviceID;          // 0x08 RO Virtio device ID
	uint32_t vendorID;          // 0x0c RO Virtio vendor ID
	uint32_t deviceFeatures;    // 0x10 RO features supported by device
	uint32_t deviceFeaturesSel; // 0x14 WO select chunk of 32 device features
	uint32_t pad18[2];
	uint32_t driverFeatures;    // 0x20 WO features activated by driver
	uint32_t driverFeaturesSel; // 0x24 WO select chunk of 32 driver features
	uint32_t pad28[2];
	uint32_t queueSel;          // 0x30 WO write here to select a virtqueue
	uint32_t queueNumMax;       // 0x34 RO device's max supported queue size
	uint32_t queueNum;          // 0x38 WO driver's chosen queue size
	uint32_t pad3c[2];
	uint32_t queueReady;        // 0x44 RW activate queue
	uint32_t pad48[2];
	uint32_t queueNotify;       // 0x50 WO write the queue num here to notify
	uint32_t pad54[3];
	uint32_t interruptStatus;   // 0x60 RO bitmask: 1 = queue; 2 = config
	uint32_t interruptACK;      // 0x64 WO write to acknowledge the interrupt
	uint32_t pad68[2];
	uint32_t status;            // 0x70 RW flags (write 0 to reset)
	uint32_t pad74[3];
	uint64_t queueDesc;         // 0x80 descriptor table physical address
	uint32_t pad88[2];
	uint64_t queueDriver;       // 0x90 available ring physical address
	uint32_t pad98[2];
	uint64_t queueDevice;       // 0xa0 used ring physical address
	uint32_t pada8[21];
	uint32_t configGeneration;  // 0xfc version of config space
	char config[];
} __attribute((scalar_storage_order("little-endian")));
