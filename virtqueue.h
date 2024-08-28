/* Copyright (c) 2023 Elliot Nunn */
/* Licensed under the MIT license */

#pragma once

#include <stdbool.h>
#include <stdint.h>

// Create a descriptor ring for this virtqueue, return actual size
uint16_t QInit(uint16_t q, uint16_t max_size);

// Never blocks, just panics if not enough descriptors available
void QSend(
	uint16_t q,
	uint16_t n_out, uint16_t n_in,
	uint32_t *phys_addrs, uint32_t *sizes,
	volatile uint32_t *retsize,
	bool wait);

// Called by transport about a change to the used ring
void QNotified(void);
