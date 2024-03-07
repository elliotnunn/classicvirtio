/* Copyright (c) 2024 Elliot Nunn */
/* Licensed under the MIT license */

// Generate Rez code
// Used by multifork-3, uses 9buf to read/write

#pragma once
#include <stdint.h>

// Write "data 'CODE' (1, purgeable) {" to file
void DerezHeader(uint8_t attrib, char *type, int16_t id, uint8_t *name);

// Read bytes from file and write lines of $"FFFF FFFF"
void DerezBody(uint32_t len);
