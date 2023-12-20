/* Copyright (c) 2023 Elliot Nunn */
/* Licensed under the MIT license */

#pragma once

#include <stddef.h>
#include <stdint.h>

// Return the page-aligned logical address
// Populate the array of physical page addresses
void *AllocPages(size_t count, uint32_t *physicalPageAddresses);

// Pass in the logical address
void FreePages(void *addr);
