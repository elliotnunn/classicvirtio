/* Copyright (c) 2023 Elliot Nunn */
/* Licensed under the MIT license */

#include "multifork.h"

struct MFImpl MFChoose(const char *suggest) {
	if (suggest[0] == '3') return MF3;

	// probably should fall back on some autodetection
	return MF3;
}
