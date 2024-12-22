/* Copyright (c) 2023 Elliot Nunn */
/* Licensed under the MIT license */

#include <stdint.h>

#include <DriverSynchronization.h>

#include <stdbool.h>

#include "ConditionalMacros.h"
#include "allocator.h"
#include "cleanup.h"
#include "device.h"
#include "interruptmask.h"
#include "panic.h"
#include "printf.h"
#include "transport.h"
#include "structs-virtqueue.h"

#include "virtqueue.h"

enum {
	MAX_VQ = 2,
	MAX_RING = 256,
};

struct virtq {
	uint16_t size;
	uint16_t used_ctr;
	struct virtq_desc *desc;
	struct virtq_avail *avail;
	struct virtq_used *used;
	volatile uint32_t *retlenptrs[MAX_RING];
};

static void poll(uint16_t q);

static volatile struct virtq queues[MAX_VQ];

uint16_t QInit(uint16_t q, uint16_t max_size) {
	if (q >= MAX_VQ) return 0;

	uint16_t size = max_size;
	if (size > MAX_RING) size = MAX_RING;
	if (size > VQueueMaxSize(q)) size = VQueueMaxSize(q);

	uint32_t phys[3];
	void *pages = AllocPages(3, phys);
	if (pages == NULL) return 0;

	// To prevent spurious DMA or interrupts, quiesce the device before freeing this queue's memory.
	// Without VIRTIO_F_RING_RESET (spec 1.2) the other queues have to die too.
	RegisterCleanupVoidPtr(FreePages, pages);
	RegisterCleanup(VReset); // remember this is registered AFTER FreePages so executed BEFORE

	// Underlying transport needs the physical addresses of the rings
	VQueueSet(q, size, phys[0], phys[1], phys[2]);

	// But we only need to keep the logical pointers
	queues[q].desc = pages;
	queues[q].avail = (void *)((char *)pages + 0x1000);
	queues[q].used = (void *)((char *)pages + 0x2000);

	queues[q].size = size;

	// Mark all descriptors free
	for (int i=0; i<queues[q].size; i++) queues[q].desc[i].next = 0xffff;

	return size;
}

void QSend(uint16_t q, uint16_t n_out, uint16_t n_in, uint32_t *addrs, uint32_t *sizes, volatile uint32_t *retsize, bool wait) {
	volatile uint32_t myval;
	if (retsize == NULL && wait) {
		retsize = &myval;
	}
	if (retsize != NULL) {
		*retsize = 0;
	}

	short sr = DisableInterrupts();

	// Reverse iterate through user's buffers, create a linked descriptor list
	uint16_t remain = n_out + n_in;
	uint16_t nextbuf = 0; // doesn't matter, there is no "next"
	for (uint16_t buf=queues[q].size-1; buf!=0xffff && remain; buf--) {
		if (queues[q].desc[buf].next != 0xffff) continue; // not a free desc

		remain--;

		queues[q].desc[buf] = (struct virtq_desc){
			.addr = addrs[remain],
			.len = sizes[remain],
			.flags =
				((remain<n_out+n_in-1) ? VIRTQ_DESC_F_NEXT : 0) |
				((remain>=n_out) ? VIRTQ_DESC_F_WRITE : 0),
			.next = nextbuf
		};

		nextbuf = buf;
	}

	// maybe this should wait for more descriptors to be available?
	if (remain) panic("attempted QSend when out of descriptors");

	queues[q].retlenptrs[nextbuf] = retsize;

	// Put a pointer to the "head" descriptor in the avail queue
	uint16_t idx = queues[q].avail->idx;
	queues[q].avail->ring[idx & (queues[q].size - 1)] = nextbuf; // first in chain
	SynchronizeIO();
	queues[q].avail->idx = idx + 1;
	SynchronizeIO();

	if (queues[q].used->flags == 0) VNotify(q);

	if (wait) {
		if (Interruptible(sr)) {
			// Block the emulator and wait efficiently
			ReenableInterruptsAndWaitFor(sr, retsize);
		} else {
			// Unavoidable poll
			do {
				poll(q);
			} while (*retsize == 0);
			ReenableInterrupts(sr);
		}
	} else {
		ReenableInterrupts(sr);
	}
}

// Called by transport at interrupt time, fear no further interruption
void QNotified(void) {
	for (uint16_t q=0; queues[q].size != 0; q++) {
		poll(q);
	}
}

// Call DNotified for each buffer in the used ring
// not reentrant, only ever called with interrupts masked
static void poll(uint16_t q) {
	uint16_t i = queues[q].used_ctr;
	uint16_t mask = queues[q].size - 1;
	uint16_t end = queues[q].used->idx;
	queues[q].used_ctr = end;

	for (; i != end; i++) {
		uint16_t first = queues[q].used->ring[i&mask].id;
		size_t len = queues[q].used->ring[i&mask].len;

		uint16_t buf = first;
		for (;;) {
			uint16_t nextbuf = queues[q].desc[buf].next;
			queues[q].desc[buf].next = 0xffff;
			if ((queues[q].desc[buf].flags & VIRTQ_DESC_F_NEXT) == 0) break;
			buf = nextbuf;
		}

		volatile uint32_t *retsize = queues[q].retlenptrs[first];
		if (retsize != NULL) {
			*retsize = len;
		}
		DNotified(q, queues[q].retlenptrs[first]);
	}
}
