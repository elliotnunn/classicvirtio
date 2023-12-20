/* Copyright (c) 2023 Elliot Nunn */
/* Licensed under the MIT license */

#pragma once

struct builtinGamma {
	char name[32];
	struct {
		 short gVersion;           // always 0
		 short gType;              // 0 means "independent from CLUT"
		 short gFormulaSize;       // display-identifying bytes after gDataWidth = 0
		 short gChanCnt;           // 1 in this case, could be 3 in others
		 short gDataCnt;           // 256
		 short gDataWidth;         // 8 bits per element
		 unsigned char data[256];
	} table;
};

extern const struct builtinGamma builtinGamma[];
extern const int builtinGammaCount;
