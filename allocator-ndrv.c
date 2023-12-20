/* Copyright (c) 2023 Elliot Nunn */
/* Licensed under the MIT license */

/*
Bugs:
- 4k pages assumed
- Memory not cache-inhibited. We assume it is coherent.
*/

#include <DriverServices.h>
#include <stddef.h>
#include <stdint.h>
#include "allocator.h"

void *AllocPages(size_t count, uint32_t *physicalPageAddresses) {
	char *unaligned = PoolAllocateResident(count*0x1000 + 0x2000, true);
	if (unaligned == NULL) return NULL;

	// This address is page aligned and has a guaranteed page below
	char *aligned = (char *)(((unsigned long)unaligned + 0x2000) & ~0xfff);

	// Place structures at known offsets below the returned pointer,
	// for cleanup by FreePages
	*(void **)(aligned - 0xf00) = unaligned;
	struct IOPreparationTable *prep = (void *)(aligned - 0x1000);

	prep->options = kIOLogicalRanges;
	prep->state = 0;
	prep->preparationID = 0;
	prep->addressSpace = kCurrentAddressSpaceID;
	prep->granularity = 0x1000 * count; // partial preparation unacceptable
	prep->firstPrepared = 0;
	prep->lengthPrepared = 0;
	prep->mappingEntryCount = count;
	prep->logicalMapping = NULL;
	prep->physicalMapping = (void *)physicalPageAddresses;
	prep->rangeInfo.range.base = aligned;
	prep->rangeInfo.range.length = 0x1000 * count;

	OSStatus err = PrepareMemoryForIO(prep);
	if (err) {
		PoolDeallocate(unaligned);
		return NULL;
	} else if ((prep->state & kIOStateDone) == 0) {
		CheckpointIO(prep->preparationID, 0);
		PoolDeallocate(unaligned);
		return NULL;
	}

	return aligned;
}

void FreePages(void *addr) {
	struct IOPreparationTable *prep = (void *)((char *)addr - 0x1000);
	void *unaligned = *(void **)((char *)addr - 0xf00);
	CheckpointIO(prep->preparationID, 0);
	PoolDeallocate(unaligned);
}
