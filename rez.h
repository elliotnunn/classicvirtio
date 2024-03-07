/* Copyright (c) 2024 Elliot Nunn */
/* Licensed under the MIT license */

// Interpret Rez code
// Used by multifork-3, uses 9buf to read/write

#pragma once
#include <stdbool.h>
#include <stdint.h>

// Both functions read Rez code via 9buf, returning a negative value on error.
// RezBody writes resource data via 9buf (returning number of bytes or negative errno).

// Read a Rez "data" directive to get its contents, or return an error code
int RezHeader(uint8_t *attrib, uint32_t *type, int16_t *id, bool *hasname, uint8_t name[256]);

// Read Rez "$" directives and write out resource data
int32_t RezBody(void);
