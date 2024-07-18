/* Copyright (c) 2023 Elliot Nunn */
/* Licensed under the MIT license */

#include "multifork.h"

struct MFImpl MF;

void MFChoose(const char *suggest) {
	MF = MF3; // temporary monoculture
}
