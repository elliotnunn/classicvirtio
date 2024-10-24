/* Copyright (c) Elliot Nunn */
/* Licensed under the MIT license */

#include <Devices.h>
#include <Files.h>

#include "tap.h"

#include "scratch.h"

short MkScratchFileAlphabetic(int size) {
    struct FileParam dpb = {.ioNamePtr="\pScratchAlphabetic"};
    PBDeleteSync((void *)&dpb); // fear not failure

	struct FileParam cpb = {.ioNamePtr="\pScratchAlphabetic"};
	if (PBCreateSync((void *)&cpb)) TAPBailOut("Could not create scratchx");;

	struct FileParam opb = {.ioNamePtr="\pScratchAlphabetic"};
	if (PBOpenSync((void *)&opb)) TAPBailOut("Could not open scratch");

	short ref = opb.ioFRefNum;

	struct IOParam wpb = {.ioRefNum=ref, .ioBuffer="abcdefghijklmnopqrstuvwxyz", .ioReqCount=size};
	if (PBWriteSync((void *)&wpb) || wpb.ioActCount != size) TAPBailOut("Coult not write scratch");

	return ref;
}
