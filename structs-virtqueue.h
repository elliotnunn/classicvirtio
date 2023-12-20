#pragma once

#include <stdint.h>

struct virtq_desc { // all little-endian
	uint32_t addr; // guest-physical
	uint32_t addr_hi;
	uint32_t len;
	uint16_t flags;
	uint16_t next;
} __attribute((scalar_storage_order("little-endian")));

/* This marks a buffer as continuing via the next field. */
#define VIRTQ_DESC_F_NEXT 1
/* This marks a buffer as device write-only (otherwise device read-only). */
#define VIRTQ_DESC_F_WRITE 2
/* This means the buffer contains a list of buffer descriptors. */
#define VIRTQ_DESC_F_INDIRECT 4

struct virtq_avail {
	uint16_t flags;
	uint16_t idx;
	uint16_t ring[999];
} __attribute((scalar_storage_order("little-endian")));

struct virtq_used_elem {
	uint16_t id; // of descriptor chain
	uint16_t pad;
	uint32_t len; // in bytes of descriptor chain
} __attribute((scalar_storage_order("little-endian")));

struct virtq_used {
	uint16_t flags;
	uint16_t idx;
	struct virtq_used_elem ring[999]; // 8 bytes each
} __attribute((scalar_storage_order("little-endian")));
