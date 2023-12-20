/* Copyright (c) 2023 Elliot Nunn */
/* Licensed under the MIT license */

/*
Bugs:
- 4k pages assumed
- Memory not cache-inhibited. We assume it is coherent.
*/

#include <Memory.h>

#include "panic.h"

#include <stddef.h>
#include <stdint.h>

#include "allocator.h"

void *AllocPages(size_t count, uint32_t *physicalPageAddresses) {
	char *unaligned = NewPtrSysClear(count*0x1000 + 0x2000);
	if (unaligned == NULL) return NULL;

	// This address is page aligned and has a guaranteed page below
	char *aligned = (char *)(((unsigned long)unaligned + 0x2000) & ~0xfff);

	// Place structures at known offsets below the returned pointer,
	// for cleanup by FreePages
	*(void **)(aligned - 0xf00) = unaligned;
	*(size_t *)(aligned - 0xefc) = count;

	if (LockMemory(aligned, count * 0x1000)) {
		DisposePtr(unaligned);
		return NULL;
	}

	// Now safe to assume that GetPhysical will work for every page
	for (size_t i=0; i<count; i++) {
		unsigned long n = 1;

		struct LogicalToPhysicalTable mapping = {
			.logical = {.address = aligned + i*0x1000, .count = 0x1000}
		};

		if (GetPhysical(&mapping, &n)) panic("GetPhysical unexpectedly failed");

		physicalPageAddresses[i] = (uint32_t)mapping.physical[0].address;
	}

	return aligned;
}

void FreePages(void *addr) {
	UnlockMemory(addr, *(size_t *)(addr - 0xefc));
	DisposePtr(*(void **)(addr - 0xf00));
}
