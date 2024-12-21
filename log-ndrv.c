#include <stdint.h>

#include <NameRegistry.h>
#include <PCI.h>

static void findLogicalBARs(RegEntryID *pciDevice, void *barArray[6]); // utility for pci

volatile char *virtioSerialRegister(void) {
	RegEntryIter cookie;
	RegEntryID dev;
	Boolean done;
	RegistryEntryIterateCreate(&cookie);
	RegistryEntrySearch(&cookie, kRegIterSubTrees, &dev, &done, "name", "pci1af4,1003", 13/*preceding str len*/);
	RegistryEntryIterateDispose(&cookie);
	if (done) {
		return NULL;
	}

	void *bars[6];
	findLogicalBARs(&dev, bars);

	// PCI configuration structures point to addresses we need within the BARs
	uint8_t cap_offset;
	for (ExpMgrConfigReadByte(&dev, (LogicalAddress)0x34, &cap_offset);
		cap_offset != 0;
		ExpMgrConfigReadByte(&dev, (LogicalAddress)(cap_offset+1), &cap_offset)) {

		uint8_t cap_vndr, cfg_type, bar;
		uint32_t offset; // within the bar

		// vendor-specific capability struct, i.e. a "VIRTIO_*" one
		ExpMgrConfigReadByte(&dev, (LogicalAddress)(uint32_t)cap_offset, &cap_vndr);
		if (cap_vndr != 9) continue;
		ExpMgrConfigReadByte(&dev, (LogicalAddress)(cap_offset+3), &cfg_type);
		ExpMgrConfigReadByte(&dev, (LogicalAddress)(cap_offset+4), &bar);
		ExpMgrConfigReadLong(&dev, (LogicalAddress)(cap_offset+8), &offset);

		if (cfg_type == 4) { // its the Virtio configuration struct
			return bars[bar] + offset + 8; // emerg_wr field
		}
	}
	return NULL;
}

// Copied from transport-ndrv.c
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
