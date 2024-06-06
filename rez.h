/* Copyright (c) 2024 Elliot Nunn */
/* Licensed under the MIT license */

// Interpret Rez code
// Used by multifork-3, uses 9buf to read/write

#pragma once

#include <stdint.h>

uint32_t Rez(uint32_t textfid, uint32_t forkfid);
