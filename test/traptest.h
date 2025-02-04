/* Copyright (c) Elliot Nunn */
/* Licensed under the MIT license */

#pragma once

#include <stdint.h>

enum {
    END = -20,
};

void TrapTest(uint32_t trapnum, ...);
void *GetFieldPtr(int field);
uint32_t GetField32(int field);
uint16_t GetField16(int field);
uint8_t GetField8(int field);
