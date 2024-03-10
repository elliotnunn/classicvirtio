/* Copyright (c) 2023 Elliot Nunn */
/* Licensed under the MIT license */

#include "multifork.h"

struct MFImpl MF;

void MFChoose(const char *suggest) {
	if (suggest[0] == '1') MF = MF1;
	else if (suggest[0] == '3') MF = MF3;
	else MF = MF3;
}
