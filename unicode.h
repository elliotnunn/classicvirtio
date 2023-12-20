/* Copyright (c) 2023 Elliot Nunn */
/* Licensed under the MIT license */

#pragma once

#include <stdbool.h>

// convert encoding (including colons & slashes), shorten if needed
// UTF-8 is null-terminated, Roman is Pascal

void mr31name(unsigned char *roman, const char *utf8);
void mr27name(unsigned char *roman, const char *utf8);
void utf8name(char *utf8, const unsigned char *roman);
long utf8char(unsigned char roman);