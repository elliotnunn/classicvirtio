/* Copyright (c) 2024 Elliot Nunn */
/* Licensed under the MIT license */

#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <PCI.h>
#include <Slots.h>

#include <NameRegistry.h>
#include "printf.h"
#include "structs-mmio.h"

#include "log.h"
#include "printf.h"

bool LogEnable;
char LogPrefix[32];
static volatile char *reg;

// These all return the address of a hardware register that prints to the outside world
static volatile char *sccRegister(void); // not used, kept around in case something breaks
static volatile char *nubusRegister(void); // if a NuBus Virtio console device is found
static volatile char *pciRegister(void); // if a PCI Virtio console device is found
static void findLogicalBARs(RegEntryID *pciDevice, void *barArray[6]); // utility for pci

void InitLog(void) {
#if GENERATINGCFM
	reg = pciRegister();
#else
	reg = nubusRegister();
#endif
	LogEnable = (reg!=NULL);
}

// proto in printf.h, called by printf.c
// For speed there is no check here, but do not call if reg is null
void _putchar(char character) {
	static bool newline = true;
	if (newline) {
		const char *p = LogPrefix;
		while (*p) *reg = *p++;
	}
	newline = (character=='\n');
	*reg = character;
}

static volatile char *sccRegister(void) {
	volatile char *acontrol = *(char **)0x1dc + 2;
	volatile char *adata = *(char **)0x1dc + 6;

	*acontrol=9;  *acontrol=0x80; // Reinit serial.... reset A/modem
	*acontrol=4;  *acontrol=0x48; // SB1 | X16CLK
	*acontrol=12; *acontrol=0;    // basic baud rate
	*acontrol=13; *acontrol=0;    // basic baud rate
	*acontrol=14; *acontrol=3;    // baud rate generator = BRSRC | BRENAB
	*acontrol=5;  *acontrol=0xca; // enable tx, 8 bits/char, set RTS & DTR

	return adata;
}

static volatile char *nubusRegister(void) {
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

static volatile char *pciRegister(void) {
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
		void *address;

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
