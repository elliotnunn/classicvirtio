/* Copyright (c) 2024 Elliot Nunn */
/* Licensed under the MIT license */

// Generate Rez code
// Used by multifork-3, uses 9buf to read/write

#pragma once

#include <stdint.h>

void DeRez(uint32_t forkfid, uint32_t textfid);