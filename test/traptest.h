/* Copyright (c) Elliot Nunn */
/* Licensed under the MIT license */

#pragma once

#include <stdint.h>

enum {
    END = -20,
};

void TrapTest(uint32_t trapnum, ...);
