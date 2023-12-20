/* Copyright (c) 2023 Elliot Nunn */
/* Licensed under the MIT license */

#include <stdint.h>

#include <DriverSynchronization.h>

#include <stdbool.h>

#include "allocator.h"
#include "atomic.h"
#include "allocator.h"
#include "device.h"
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
	int32_t interest;
	struct virtq_desc *desc;
	struct virtq_avail *avail;
	struct virtq_used *used;
	void *tag[MAX_RING];
};

static struct virtq queues[MAX_VQ];
static void QSendAtomicPart(uint16_t q, uint16_t n_out, uint16_t n_in, uint32_t *addrs, uint32_t *sizes, void *tag, bool *ret);
static void QInterestAtomicPart(uint16_t q, int32_t delta);

uint16_t QInit(uint16_t q, uint16_t max_size) {
	if (q > MAX_VQ) return 0;

	uint16_t size = max_size;
	if (size > MAX_RING) size = MAX_RING;
	if (size > VQueueMaxSize(q)) size = VQueueMaxSize(q);

	uint32_t phys[3];
	void *pages = AllocPages(3, phys);
	if (pages == NULL) return 0;

	// Underlying transport needs the physical addresses of the rings
	VQueueSet(q, size, phys[0], phys[1], phys[2]);

	// But we only need to keep the logical pointers
	queues[q].desc = pages;
	queues[q].avail = (void *)((char *)pages + 0x1000);
	queues[q].used = (void *)((char *)pages + 0x2000);

	queues[q].size = size;

	// Mark all descriptors free
	for (int i=0; i<queues[q].size; i++) queues[q].desc[i].next = 0xffff;

	// Disable notifications until QInterest
	queues[q].avail->flags = 1;

	return size;
}

bool QSend(uint16_t q, uint16_t n_out, uint16_t n_in, uint32_t *addrs, uint32_t *sizes, void *tag) {
	bool ret;
	QSendAtomicPart(q, n_out, n_in, addrs, sizes, tag, &ret);
	return ret;
}

static void QSendAtomicPart(uint16_t q, uint16_t n_out, uint16_t n_in, uint32_t *addrs, uint32_t *sizes, void *tag, bool *ret) {
	*ret = false;

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

	if (remain) panic("attempted QSend when out of descriptors");

	queues[q].tag[nextbuf] = tag;

	// Put a pointer to the "head" descriptor in the avail queue
	uint16_t idx = queues[q].avail->idx;
	queues[q].avail->ring[idx & (queues[q].size - 1)] = nextbuf; // first in chain
	SynchronizeIO();
	queues[q].avail->idx = idx + 1;
	SynchronizeIO();

	*ret = true;
	return;
}

void QNotify(uint16_t q) {
	if (queues[q].used->flags == 0) VNotify(q);
}

void QInterest(uint16_t q, int32_t delta) {
	ATOMIC2(QInterestAtomicPart, q, delta);
}

static void QInterestAtomicPart(uint16_t q, int32_t delta) {
	queues[q].interest += delta;
	queues[q].avail->flags = (queues[q].interest == 0);
}

// Called by transport hardware interrupt to reduce chance of redundant interrupts
void QDisarm(void) {
	for (uint16_t q=0; queues[q].size != 0; q++) {
		queues[q].avail->flags = 1;
	}
	SynchronizeIO();
}

// Called by transport at "deferred" or "secondary" interrupt time
void QNotified(void) {
	for (uint16_t q=0; queues[q].size != 0; q++) {
		QPoll(q);
	}

	VRearm();
	for (uint16_t q=0; queues[q].size != 0; q++) {
		queues[q].avail->flags = queues[q].interest == 0;
	}
	SynchronizeIO();

	for (uint16_t q=0; queues[q].size != 0; q++) {
		QPoll(q);
	}
}

// Call DNotified for each buffer in the used ring
void QPoll(uint16_t q) {
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

		DNotified(q, len, queues[q].tag[first]);
	}
}
