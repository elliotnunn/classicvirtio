/* Copyright (c) Elliot Nunn */
/* Licensed under the MIT license */

#pragma once

void InitScratch(void);
long MkScratchTree(const char *spec);
long ScratchDirID(const char *name);
const char *ScratchNameOfDirID(long id);
short ScratchWD(const char *name);
const char *ScratchNameOfWD(short wd);
short VolRef(void);
void SubVolName(unsigned char *pstring);
short MkScratchFileAlphabetic(int size);
