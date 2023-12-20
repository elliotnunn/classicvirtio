/* Copyright (c) 2023 Elliot Nunn */
/* Licensed under the MIT license */

#pragma once

#include <stdbool.h>
#include <stdint.h>

// Create a descriptor ring for this virtqueue, return actual size
uint16_t QInit(uint16_t q, uint16_t max_size);

// Never blocks, just returns false if not enough descriptors available
bool QSend(
	uint16_t q,
	uint16_t n_out, uint16_t n_in,
	uint32_t *phys_addrs, uint32_t *sizes,
	void *tag);

// Call after one or more QSend()s
void QNotify(uint16_t q);

// Call to increment or decrement the queue interest counter,
// which if >0 will cause movement on the queue to trigger an interrupt.
void QInterest(uint16_t q, int32_t delta);

// Called by transport to reduce chance of redundant interrupts
void QDisarm(void);

// Called by transport about a change to the used ring
void QNotified(void);

// Call DNotified for each buffer in the used ring
void QPoll(uint16_t q);
