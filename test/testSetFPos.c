/* Copyright (c) Elliot Nunn */
/* Licensed under the MIT license */

#include <stdbool.h>
#include <stdio.h>

#include <Devices.h>
#include <Files.h>

#include "constnames.h"
#include "scratch.h"
#include "tap.h"

void testSetFPos(void) {
	puts("# Testing SetFPos");
	puts("# (Note: System 7 HFS permits seeking to negative file offsets!)");

	struct line {
		int size, initialpos, mode, delta, finalpos, err;
	};

	struct line lines[] = {
		{.size=0, .initialpos=0, .mode=fsAtMark, .delta=0, .finalpos=0, .err=noErr},
		{.size=1, .initialpos=0, .mode=fsAtMark, .delta=0, .finalpos=0, .err=noErr},
		{.size=1, .initialpos=1, .mode=fsAtMark, .delta=0, .finalpos=1, .err=noErr},
		{.size=2, .initialpos=0, .mode=fsAtMark, .delta=0, .finalpos=0, .err=noErr},
		{.size=2, .initialpos=1, .mode=fsAtMark, .delta=0, .finalpos=1, .err=noErr},
		{.size=2, .initialpos=2, .mode=fsAtMark, .delta=0, .finalpos=2, .err=noErr},
		{.size=3, .initialpos=0, .mode=fsAtMark, .delta=0, .finalpos=0, .err=noErr},
		{.size=3, .initialpos=1, .mode=fsAtMark, .delta=0, .finalpos=1, .err=noErr},
		{.size=3, .initialpos=2, .mode=fsAtMark, .delta=0, .finalpos=2, .err=noErr},
		{.size=3, .initialpos=3, .mode=fsAtMark, .delta=0, .finalpos=3, .err=noErr},
		{.size=4, .initialpos=0, .mode=fsAtMark, .delta=0, .finalpos=0, .err=noErr},
		{.size=4, .initialpos=1, .mode=fsAtMark, .delta=0, .finalpos=1, .err=noErr},
		{.size=4, .initialpos=2, .mode=fsAtMark, .delta=0, .finalpos=2, .err=noErr},
		{.size=4, .initialpos=3, .mode=fsAtMark, .delta=0, .finalpos=3, .err=noErr},
		{.size=4, .initialpos=4, .mode=fsAtMark, .delta=0, .finalpos=4, .err=noErr},
		{.size=0, .initialpos=0, .mode=fsFromStart, .delta=-5, .finalpos=0, .err=posErr},
		{.size=0, .initialpos=0, .mode=fsFromStart, .delta=-4, .finalpos=0, .err=posErr},
		{.size=0, .initialpos=0, .mode=fsFromStart, .delta=-3, .finalpos=0, .err=posErr},
		{.size=0, .initialpos=0, .mode=fsFromStart, .delta=-2, .finalpos=0, .err=posErr},
		{.size=0, .initialpos=0, .mode=fsFromStart, .delta=-1, .finalpos=0, .err=posErr},
		{.size=0, .initialpos=0, .mode=fsFromStart, .delta=0, .finalpos=0, .err=noErr},
		{.size=0, .initialpos=0, .mode=fsFromStart, .delta=1, .finalpos=0, .err=eofErr},
		{.size=0, .initialpos=0, .mode=fsFromStart, .delta=2, .finalpos=0, .err=eofErr},
		{.size=0, .initialpos=0, .mode=fsFromStart, .delta=3, .finalpos=0, .err=eofErr},
		{.size=0, .initialpos=0, .mode=fsFromStart, .delta=4, .finalpos=0, .err=eofErr},
		{.size=0, .initialpos=0, .mode=fsFromStart, .delta=5, .finalpos=0, .err=eofErr},
		{.size=1, .initialpos=0, .mode=fsFromStart, .delta=-5, .finalpos=0, .err=posErr},
		{.size=1, .initialpos=0, .mode=fsFromStart, .delta=-4, .finalpos=0, .err=posErr},
		{.size=1, .initialpos=0, .mode=fsFromStart, .delta=-3, .finalpos=0, .err=posErr},
		{.size=1, .initialpos=0, .mode=fsFromStart, .delta=-2, .finalpos=0, .err=posErr},
		{.size=1, .initialpos=0, .mode=fsFromStart, .delta=-1, .finalpos=0, .err=posErr},
		{.size=1, .initialpos=0, .mode=fsFromStart, .delta=0, .finalpos=0, .err=noErr},
		{.size=1, .initialpos=0, .mode=fsFromStart, .delta=1, .finalpos=1, .err=noErr},
		{.size=1, .initialpos=0, .mode=fsFromStart, .delta=2, .finalpos=1, .err=eofErr},
		{.size=1, .initialpos=0, .mode=fsFromStart, .delta=3, .finalpos=1, .err=eofErr},
		{.size=1, .initialpos=0, .mode=fsFromStart, .delta=4, .finalpos=1, .err=eofErr},
		{.size=1, .initialpos=0, .mode=fsFromStart, .delta=5, .finalpos=1, .err=eofErr},
		{.size=2, .initialpos=0, .mode=fsFromStart, .delta=-5, .finalpos=0, .err=posErr},
		{.size=2, .initialpos=0, .mode=fsFromStart, .delta=-4, .finalpos=0, .err=posErr},
		{.size=2, .initialpos=0, .mode=fsFromStart, .delta=-3, .finalpos=0, .err=posErr},
		{.size=2, .initialpos=0, .mode=fsFromStart, .delta=-2, .finalpos=0, .err=posErr},
		{.size=2, .initialpos=0, .mode=fsFromStart, .delta=-1, .finalpos=0, .err=posErr},
		{.size=2, .initialpos=0, .mode=fsFromStart, .delta=0, .finalpos=0, .err=noErr},
		{.size=2, .initialpos=0, .mode=fsFromStart, .delta=1, .finalpos=1, .err=noErr},
		{.size=2, .initialpos=0, .mode=fsFromStart, .delta=2, .finalpos=2, .err=noErr},
		{.size=2, .initialpos=0, .mode=fsFromStart, .delta=3, .finalpos=2, .err=eofErr},
		{.size=2, .initialpos=0, .mode=fsFromStart, .delta=4, .finalpos=2, .err=eofErr},
		{.size=2, .initialpos=0, .mode=fsFromStart, .delta=5, .finalpos=2, .err=eofErr},
		{.size=3, .initialpos=0, .mode=fsFromStart, .delta=-5, .finalpos=0, .err=posErr},
		{.size=3, .initialpos=0, .mode=fsFromStart, .delta=-4, .finalpos=0, .err=posErr},
		{.size=3, .initialpos=0, .mode=fsFromStart, .delta=-3, .finalpos=0, .err=posErr},
		{.size=3, .initialpos=0, .mode=fsFromStart, .delta=-2, .finalpos=0, .err=posErr},
		{.size=3, .initialpos=0, .mode=fsFromStart, .delta=-1, .finalpos=0, .err=posErr},
		{.size=3, .initialpos=0, .mode=fsFromStart, .delta=0, .finalpos=0, .err=noErr},
		{.size=3, .initialpos=0, .mode=fsFromStart, .delta=1, .finalpos=1, .err=noErr},
		{.size=3, .initialpos=0, .mode=fsFromStart, .delta=2, .finalpos=2, .err=noErr},
		{.size=3, .initialpos=0, .mode=fsFromStart, .delta=3, .finalpos=3, .err=noErr},
		{.size=3, .initialpos=0, .mode=fsFromStart, .delta=4, .finalpos=3, .err=eofErr},
		{.size=3, .initialpos=0, .mode=fsFromStart, .delta=5, .finalpos=3, .err=eofErr},
		{.size=4, .initialpos=0, .mode=fsFromStart, .delta=-5, .finalpos=0, .err=posErr},
		{.size=4, .initialpos=0, .mode=fsFromStart, .delta=-4, .finalpos=0, .err=posErr},
		{.size=4, .initialpos=0, .mode=fsFromStart, .delta=-3, .finalpos=0, .err=posErr},
		{.size=4, .initialpos=0, .mode=fsFromStart, .delta=-2, .finalpos=0, .err=posErr},
		{.size=4, .initialpos=0, .mode=fsFromStart, .delta=-1, .finalpos=0, .err=posErr},
		{.size=4, .initialpos=0, .mode=fsFromStart, .delta=0, .finalpos=0, .err=noErr},
		{.size=4, .initialpos=0, .mode=fsFromStart, .delta=1, .finalpos=1, .err=noErr},
		{.size=4, .initialpos=0, .mode=fsFromStart, .delta=2, .finalpos=2, .err=noErr},
		{.size=4, .initialpos=0, .mode=fsFromStart, .delta=3, .finalpos=3, .err=noErr},
		{.size=4, .initialpos=0, .mode=fsFromStart, .delta=4, .finalpos=4, .err=noErr},
		{.size=4, .initialpos=0, .mode=fsFromStart, .delta=5, .finalpos=4, .err=eofErr},
		{.size=0, .initialpos=0, .mode=fsFromLEOF, .delta=-5, .finalpos=0, .err=posErr},
		{.size=0, .initialpos=0, .mode=fsFromLEOF, .delta=-4, .finalpos=0, .err=posErr},
		{.size=0, .initialpos=0, .mode=fsFromLEOF, .delta=-3, .finalpos=0, .err=posErr},
		{.size=0, .initialpos=0, .mode=fsFromLEOF, .delta=-2, .finalpos=0, .err=posErr},
		{.size=0, .initialpos=0, .mode=fsFromLEOF, .delta=-1, .finalpos=0, .err=posErr},
		{.size=0, .initialpos=0, .mode=fsFromLEOF, .delta=0, .finalpos=0, .err=noErr},
		{.size=0, .initialpos=0, .mode=fsFromLEOF, .delta=1, .finalpos=0, .err=eofErr},
		{.size=0, .initialpos=0, .mode=fsFromLEOF, .delta=2, .finalpos=0, .err=eofErr},
		{.size=0, .initialpos=0, .mode=fsFromLEOF, .delta=3, .finalpos=0, .err=eofErr},
		{.size=0, .initialpos=0, .mode=fsFromLEOF, .delta=4, .finalpos=0, .err=eofErr},
		{.size=0, .initialpos=0, .mode=fsFromLEOF, .delta=5, .finalpos=0, .err=eofErr},
		{.size=1, .initialpos=0, .mode=fsFromLEOF, .delta=-5, .finalpos=0, .err=posErr},
		{.size=1, .initialpos=0, .mode=fsFromLEOF, .delta=-4, .finalpos=0, .err=posErr},
		{.size=1, .initialpos=0, .mode=fsFromLEOF, .delta=-3, .finalpos=0, .err=posErr},
		{.size=1, .initialpos=0, .mode=fsFromLEOF, .delta=-2, .finalpos=0, .err=posErr},
		{.size=1, .initialpos=0, .mode=fsFromLEOF, .delta=-1, .finalpos=0, .err=noErr},
		{.size=1, .initialpos=0, .mode=fsFromLEOF, .delta=0, .finalpos=1, .err=noErr},
		{.size=1, .initialpos=0, .mode=fsFromLEOF, .delta=1, .finalpos=1, .err=eofErr},
		{.size=1, .initialpos=0, .mode=fsFromLEOF, .delta=2, .finalpos=1, .err=eofErr},
		{.size=1, .initialpos=0, .mode=fsFromLEOF, .delta=3, .finalpos=1, .err=eofErr},
		{.size=1, .initialpos=0, .mode=fsFromLEOF, .delta=4, .finalpos=1, .err=eofErr},
		{.size=1, .initialpos=0, .mode=fsFromLEOF, .delta=5, .finalpos=1, .err=eofErr},
		{.size=2, .initialpos=0, .mode=fsFromLEOF, .delta=-5, .finalpos=0, .err=posErr},
		{.size=2, .initialpos=0, .mode=fsFromLEOF, .delta=-4, .finalpos=0, .err=posErr},
		{.size=2, .initialpos=0, .mode=fsFromLEOF, .delta=-3, .finalpos=0, .err=posErr},
		{.size=2, .initialpos=0, .mode=fsFromLEOF, .delta=-2, .finalpos=0, .err=noErr},
		{.size=2, .initialpos=0, .mode=fsFromLEOF, .delta=-1, .finalpos=1, .err=noErr},
		{.size=2, .initialpos=0, .mode=fsFromLEOF, .delta=0, .finalpos=2, .err=noErr},
		{.size=2, .initialpos=0, .mode=fsFromLEOF, .delta=1, .finalpos=2, .err=eofErr},
		{.size=2, .initialpos=0, .mode=fsFromLEOF, .delta=2, .finalpos=2, .err=eofErr},
		{.size=2, .initialpos=0, .mode=fsFromLEOF, .delta=3, .finalpos=2, .err=eofErr},
		{.size=2, .initialpos=0, .mode=fsFromLEOF, .delta=4, .finalpos=2, .err=eofErr},
		{.size=2, .initialpos=0, .mode=fsFromLEOF, .delta=5, .finalpos=2, .err=eofErr},
		{.size=3, .initialpos=0, .mode=fsFromLEOF, .delta=-5, .finalpos=0, .err=posErr},
		{.size=3, .initialpos=0, .mode=fsFromLEOF, .delta=-4, .finalpos=0, .err=posErr},
		{.size=3, .initialpos=0, .mode=fsFromLEOF, .delta=-3, .finalpos=0, .err=noErr},
		{.size=3, .initialpos=0, .mode=fsFromLEOF, .delta=-2, .finalpos=1, .err=noErr},
		{.size=3, .initialpos=0, .mode=fsFromLEOF, .delta=-1, .finalpos=2, .err=noErr},
		{.size=3, .initialpos=0, .mode=fsFromLEOF, .delta=0, .finalpos=3, .err=noErr},
		{.size=3, .initialpos=0, .mode=fsFromLEOF, .delta=1, .finalpos=3, .err=eofErr},
		{.size=3, .initialpos=0, .mode=fsFromLEOF, .delta=2, .finalpos=3, .err=eofErr},
		{.size=3, .initialpos=0, .mode=fsFromLEOF, .delta=3, .finalpos=3, .err=eofErr},
		{.size=3, .initialpos=0, .mode=fsFromLEOF, .delta=4, .finalpos=3, .err=eofErr},
		{.size=3, .initialpos=0, .mode=fsFromLEOF, .delta=5, .finalpos=3, .err=eofErr},
		{.size=4, .initialpos=0, .mode=fsFromLEOF, .delta=-5, .finalpos=0, .err=posErr},
		{.size=4, .initialpos=0, .mode=fsFromLEOF, .delta=-4, .finalpos=0, .err=noErr},
		{.size=4, .initialpos=0, .mode=fsFromLEOF, .delta=-3, .finalpos=1, .err=noErr},
		{.size=4, .initialpos=0, .mode=fsFromLEOF, .delta=-2, .finalpos=2, .err=noErr},
		{.size=4, .initialpos=0, .mode=fsFromLEOF, .delta=-1, .finalpos=3, .err=noErr},
		{.size=4, .initialpos=0, .mode=fsFromLEOF, .delta=0, .finalpos=4, .err=noErr},
		{.size=4, .initialpos=0, .mode=fsFromLEOF, .delta=1, .finalpos=4, .err=eofErr},
		{.size=4, .initialpos=0, .mode=fsFromLEOF, .delta=2, .finalpos=4, .err=eofErr},
		{.size=4, .initialpos=0, .mode=fsFromLEOF, .delta=3, .finalpos=4, .err=eofErr},
		{.size=4, .initialpos=0, .mode=fsFromLEOF, .delta=4, .finalpos=4, .err=eofErr},
		{.size=4, .initialpos=0, .mode=fsFromLEOF, .delta=5, .finalpos=4, .err=eofErr},
		{.size=0, .initialpos=0, .mode=fsFromMark, .delta=-5, .finalpos=0, .err=posErr},
		{.size=0, .initialpos=0, .mode=fsFromMark, .delta=-4, .finalpos=0, .err=posErr},
		{.size=0, .initialpos=0, .mode=fsFromMark, .delta=-3, .finalpos=0, .err=posErr},
		{.size=0, .initialpos=0, .mode=fsFromMark, .delta=-2, .finalpos=0, .err=posErr},
		{.size=0, .initialpos=0, .mode=fsFromMark, .delta=-1, .finalpos=0, .err=posErr},
		{.size=0, .initialpos=0, .mode=fsFromMark, .delta=0, .finalpos=0, .err=noErr},
		{.size=0, .initialpos=0, .mode=fsFromMark, .delta=1, .finalpos=0, .err=eofErr},
		{.size=0, .initialpos=0, .mode=fsFromMark, .delta=2, .finalpos=0, .err=eofErr},
		{.size=0, .initialpos=0, .mode=fsFromMark, .delta=3, .finalpos=0, .err=eofErr},
		{.size=0, .initialpos=0, .mode=fsFromMark, .delta=4, .finalpos=0, .err=eofErr},
		{.size=0, .initialpos=0, .mode=fsFromMark, .delta=5, .finalpos=0, .err=eofErr},
		{.size=1, .initialpos=0, .mode=fsFromMark, .delta=-5, .finalpos=0, .err=posErr},
		{.size=1, .initialpos=0, .mode=fsFromMark, .delta=-4, .finalpos=0, .err=posErr},
		{.size=1, .initialpos=0, .mode=fsFromMark, .delta=-3, .finalpos=0, .err=posErr},
		{.size=1, .initialpos=0, .mode=fsFromMark, .delta=-2, .finalpos=0, .err=posErr},
		{.size=1, .initialpos=0, .mode=fsFromMark, .delta=-1, .finalpos=0, .err=posErr},
		{.size=1, .initialpos=0, .mode=fsFromMark, .delta=0, .finalpos=0, .err=noErr},
		{.size=1, .initialpos=0, .mode=fsFromMark, .delta=1, .finalpos=1, .err=noErr},
		{.size=1, .initialpos=0, .mode=fsFromMark, .delta=2, .finalpos=1, .err=eofErr},
		{.size=1, .initialpos=0, .mode=fsFromMark, .delta=3, .finalpos=1, .err=eofErr},
		{.size=1, .initialpos=0, .mode=fsFromMark, .delta=4, .finalpos=1, .err=eofErr},
		{.size=1, .initialpos=0, .mode=fsFromMark, .delta=5, .finalpos=1, .err=eofErr},
		{.size=1, .initialpos=1, .mode=fsFromMark, .delta=-5, .finalpos=1, .err=posErr},
		{.size=1, .initialpos=1, .mode=fsFromMark, .delta=-4, .finalpos=1, .err=posErr},
		{.size=1, .initialpos=1, .mode=fsFromMark, .delta=-3, .finalpos=1, .err=posErr},
		{.size=1, .initialpos=1, .mode=fsFromMark, .delta=-2, .finalpos=1, .err=posErr},
		{.size=1, .initialpos=1, .mode=fsFromMark, .delta=-1, .finalpos=0, .err=noErr},
		{.size=1, .initialpos=1, .mode=fsFromMark, .delta=0, .finalpos=1, .err=noErr},
		{.size=1, .initialpos=1, .mode=fsFromMark, .delta=1, .finalpos=1, .err=eofErr},
		{.size=1, .initialpos=1, .mode=fsFromMark, .delta=2, .finalpos=1, .err=eofErr},
		{.size=1, .initialpos=1, .mode=fsFromMark, .delta=3, .finalpos=1, .err=eofErr},
		{.size=1, .initialpos=1, .mode=fsFromMark, .delta=4, .finalpos=1, .err=eofErr},
		{.size=1, .initialpos=1, .mode=fsFromMark, .delta=5, .finalpos=1, .err=eofErr},
		{.size=2, .initialpos=0, .mode=fsFromMark, .delta=-5, .finalpos=0, .err=posErr},
		{.size=2, .initialpos=0, .mode=fsFromMark, .delta=-4, .finalpos=0, .err=posErr},
		{.size=2, .initialpos=0, .mode=fsFromMark, .delta=-3, .finalpos=0, .err=posErr},
		{.size=2, .initialpos=0, .mode=fsFromMark, .delta=-2, .finalpos=0, .err=posErr},
		{.size=2, .initialpos=0, .mode=fsFromMark, .delta=-1, .finalpos=0, .err=posErr},
		{.size=2, .initialpos=0, .mode=fsFromMark, .delta=0, .finalpos=0, .err=noErr},
		{.size=2, .initialpos=0, .mode=fsFromMark, .delta=1, .finalpos=1, .err=noErr},
		{.size=2, .initialpos=0, .mode=fsFromMark, .delta=2, .finalpos=2, .err=noErr},
		{.size=2, .initialpos=0, .mode=fsFromMark, .delta=3, .finalpos=2, .err=eofErr},
		{.size=2, .initialpos=0, .mode=fsFromMark, .delta=4, .finalpos=2, .err=eofErr},
		{.size=2, .initialpos=0, .mode=fsFromMark, .delta=5, .finalpos=2, .err=eofErr},
		{.size=2, .initialpos=1, .mode=fsFromMark, .delta=-5, .finalpos=1, .err=posErr},
		{.size=2, .initialpos=1, .mode=fsFromMark, .delta=-4, .finalpos=1, .err=posErr},
		{.size=2, .initialpos=1, .mode=fsFromMark, .delta=-3, .finalpos=1, .err=posErr},
		{.size=2, .initialpos=1, .mode=fsFromMark, .delta=-2, .finalpos=1, .err=posErr},
		{.size=2, .initialpos=1, .mode=fsFromMark, .delta=-1, .finalpos=0, .err=noErr},
		{.size=2, .initialpos=1, .mode=fsFromMark, .delta=0, .finalpos=1, .err=noErr},
		{.size=2, .initialpos=1, .mode=fsFromMark, .delta=1, .finalpos=2, .err=noErr},
		{.size=2, .initialpos=1, .mode=fsFromMark, .delta=2, .finalpos=2, .err=eofErr},
		{.size=2, .initialpos=1, .mode=fsFromMark, .delta=3, .finalpos=2, .err=eofErr},
		{.size=2, .initialpos=1, .mode=fsFromMark, .delta=4, .finalpos=2, .err=eofErr},
		{.size=2, .initialpos=1, .mode=fsFromMark, .delta=5, .finalpos=2, .err=eofErr},
		{.size=2, .initialpos=2, .mode=fsFromMark, .delta=-5, .finalpos=2, .err=posErr},
		{.size=2, .initialpos=2, .mode=fsFromMark, .delta=-4, .finalpos=2, .err=posErr},
		{.size=2, .initialpos=2, .mode=fsFromMark, .delta=-3, .finalpos=2, .err=posErr},
		{.size=2, .initialpos=2, .mode=fsFromMark, .delta=-2, .finalpos=0, .err=noErr},
		{.size=2, .initialpos=2, .mode=fsFromMark, .delta=-1, .finalpos=1, .err=noErr},
		{.size=2, .initialpos=2, .mode=fsFromMark, .delta=0, .finalpos=2, .err=noErr},
		{.size=2, .initialpos=2, .mode=fsFromMark, .delta=1, .finalpos=2, .err=eofErr},
		{.size=2, .initialpos=2, .mode=fsFromMark, .delta=2, .finalpos=2, .err=eofErr},
		{.size=2, .initialpos=2, .mode=fsFromMark, .delta=3, .finalpos=2, .err=eofErr},
		{.size=2, .initialpos=2, .mode=fsFromMark, .delta=4, .finalpos=2, .err=eofErr},
		{.size=2, .initialpos=2, .mode=fsFromMark, .delta=5, .finalpos=2, .err=eofErr},
		{.size=3, .initialpos=0, .mode=fsFromMark, .delta=-5, .finalpos=0, .err=posErr},
		{.size=3, .initialpos=0, .mode=fsFromMark, .delta=-4, .finalpos=0, .err=posErr},
		{.size=3, .initialpos=0, .mode=fsFromMark, .delta=-3, .finalpos=0, .err=posErr},
		{.size=3, .initialpos=0, .mode=fsFromMark, .delta=-2, .finalpos=0, .err=posErr},
		{.size=3, .initialpos=0, .mode=fsFromMark, .delta=-1, .finalpos=0, .err=posErr},
		{.size=3, .initialpos=0, .mode=fsFromMark, .delta=0, .finalpos=0, .err=noErr},
		{.size=3, .initialpos=0, .mode=fsFromMark, .delta=1, .finalpos=1, .err=noErr},
		{.size=3, .initialpos=0, .mode=fsFromMark, .delta=2, .finalpos=2, .err=noErr},
		{.size=3, .initialpos=0, .mode=fsFromMark, .delta=3, .finalpos=3, .err=noErr},
		{.size=3, .initialpos=0, .mode=fsFromMark, .delta=4, .finalpos=3, .err=eofErr},
		{.size=3, .initialpos=0, .mode=fsFromMark, .delta=5, .finalpos=3, .err=eofErr},
		{.size=3, .initialpos=1, .mode=fsFromMark, .delta=-5, .finalpos=1, .err=posErr},
		{.size=3, .initialpos=1, .mode=fsFromMark, .delta=-4, .finalpos=1, .err=posErr},
		{.size=3, .initialpos=1, .mode=fsFromMark, .delta=-3, .finalpos=1, .err=posErr},
		{.size=3, .initialpos=1, .mode=fsFromMark, .delta=-2, .finalpos=1, .err=posErr},
		{.size=3, .initialpos=1, .mode=fsFromMark, .delta=-1, .finalpos=0, .err=noErr},
		{.size=3, .initialpos=1, .mode=fsFromMark, .delta=0, .finalpos=1, .err=noErr},
		{.size=3, .initialpos=1, .mode=fsFromMark, .delta=1, .finalpos=2, .err=noErr},
		{.size=3, .initialpos=1, .mode=fsFromMark, .delta=2, .finalpos=3, .err=noErr},
		{.size=3, .initialpos=1, .mode=fsFromMark, .delta=3, .finalpos=3, .err=eofErr},
		{.size=3, .initialpos=1, .mode=fsFromMark, .delta=4, .finalpos=3, .err=eofErr},
		{.size=3, .initialpos=1, .mode=fsFromMark, .delta=5, .finalpos=3, .err=eofErr},
		{.size=3, .initialpos=2, .mode=fsFromMark, .delta=-5, .finalpos=2, .err=posErr},
		{.size=3, .initialpos=2, .mode=fsFromMark, .delta=-4, .finalpos=2, .err=posErr},
		{.size=3, .initialpos=2, .mode=fsFromMark, .delta=-3, .finalpos=2, .err=posErr},
		{.size=3, .initialpos=2, .mode=fsFromMark, .delta=-2, .finalpos=0, .err=noErr},
		{.size=3, .initialpos=2, .mode=fsFromMark, .delta=-1, .finalpos=1, .err=noErr},
		{.size=3, .initialpos=2, .mode=fsFromMark, .delta=0, .finalpos=2, .err=noErr},
		{.size=3, .initialpos=2, .mode=fsFromMark, .delta=1, .finalpos=3, .err=noErr},
		{.size=3, .initialpos=2, .mode=fsFromMark, .delta=2, .finalpos=3, .err=eofErr},
		{.size=3, .initialpos=2, .mode=fsFromMark, .delta=3, .finalpos=3, .err=eofErr},
		{.size=3, .initialpos=2, .mode=fsFromMark, .delta=4, .finalpos=3, .err=eofErr},
		{.size=3, .initialpos=2, .mode=fsFromMark, .delta=5, .finalpos=3, .err=eofErr},
		{.size=3, .initialpos=3, .mode=fsFromMark, .delta=-5, .finalpos=3, .err=posErr},
		{.size=3, .initialpos=3, .mode=fsFromMark, .delta=-4, .finalpos=3, .err=posErr},
		{.size=3, .initialpos=3, .mode=fsFromMark, .delta=-3, .finalpos=0, .err=noErr},
		{.size=3, .initialpos=3, .mode=fsFromMark, .delta=-2, .finalpos=1, .err=noErr},
		{.size=3, .initialpos=3, .mode=fsFromMark, .delta=-1, .finalpos=2, .err=noErr},
		{.size=3, .initialpos=3, .mode=fsFromMark, .delta=0, .finalpos=3, .err=noErr},
		{.size=3, .initialpos=3, .mode=fsFromMark, .delta=1, .finalpos=3, .err=eofErr},
		{.size=3, .initialpos=3, .mode=fsFromMark, .delta=2, .finalpos=3, .err=eofErr},
		{.size=3, .initialpos=3, .mode=fsFromMark, .delta=3, .finalpos=3, .err=eofErr},
		{.size=3, .initialpos=3, .mode=fsFromMark, .delta=4, .finalpos=3, .err=eofErr},
		{.size=3, .initialpos=3, .mode=fsFromMark, .delta=5, .finalpos=3, .err=eofErr},
		{.size=4, .initialpos=0, .mode=fsFromMark, .delta=-5, .finalpos=0, .err=posErr},
		{.size=4, .initialpos=0, .mode=fsFromMark, .delta=-4, .finalpos=0, .err=posErr},
		{.size=4, .initialpos=0, .mode=fsFromMark, .delta=-3, .finalpos=0, .err=posErr},
		{.size=4, .initialpos=0, .mode=fsFromMark, .delta=-2, .finalpos=0, .err=posErr},
		{.size=4, .initialpos=0, .mode=fsFromMark, .delta=-1, .finalpos=0, .err=posErr},
		{.size=4, .initialpos=0, .mode=fsFromMark, .delta=0, .finalpos=0, .err=noErr},
		{.size=4, .initialpos=0, .mode=fsFromMark, .delta=1, .finalpos=1, .err=noErr},
		{.size=4, .initialpos=0, .mode=fsFromMark, .delta=2, .finalpos=2, .err=noErr},
		{.size=4, .initialpos=0, .mode=fsFromMark, .delta=3, .finalpos=3, .err=noErr},
		{.size=4, .initialpos=0, .mode=fsFromMark, .delta=4, .finalpos=4, .err=noErr},
		{.size=4, .initialpos=0, .mode=fsFromMark, .delta=5, .finalpos=4, .err=eofErr},
		{.size=4, .initialpos=1, .mode=fsFromMark, .delta=-5, .finalpos=1, .err=posErr},
		{.size=4, .initialpos=1, .mode=fsFromMark, .delta=-4, .finalpos=1, .err=posErr},
		{.size=4, .initialpos=1, .mode=fsFromMark, .delta=-3, .finalpos=1, .err=posErr},
		{.size=4, .initialpos=1, .mode=fsFromMark, .delta=-2, .finalpos=1, .err=posErr},
		{.size=4, .initialpos=1, .mode=fsFromMark, .delta=-1, .finalpos=0, .err=noErr},
		{.size=4, .initialpos=1, .mode=fsFromMark, .delta=0, .finalpos=1, .err=noErr},
		{.size=4, .initialpos=1, .mode=fsFromMark, .delta=1, .finalpos=2, .err=noErr},
		{.size=4, .initialpos=1, .mode=fsFromMark, .delta=2, .finalpos=3, .err=noErr},
		{.size=4, .initialpos=1, .mode=fsFromMark, .delta=3, .finalpos=4, .err=noErr},
		{.size=4, .initialpos=1, .mode=fsFromMark, .delta=4, .finalpos=4, .err=eofErr},
		{.size=4, .initialpos=1, .mode=fsFromMark, .delta=5, .finalpos=4, .err=eofErr},
		{.size=4, .initialpos=2, .mode=fsFromMark, .delta=-5, .finalpos=2, .err=posErr},
		{.size=4, .initialpos=2, .mode=fsFromMark, .delta=-4, .finalpos=2, .err=posErr},
		{.size=4, .initialpos=2, .mode=fsFromMark, .delta=-3, .finalpos=2, .err=posErr},
		{.size=4, .initialpos=2, .mode=fsFromMark, .delta=-2, .finalpos=0, .err=noErr},
		{.size=4, .initialpos=2, .mode=fsFromMark, .delta=-1, .finalpos=1, .err=noErr},
		{.size=4, .initialpos=2, .mode=fsFromMark, .delta=0, .finalpos=2, .err=noErr},
		{.size=4, .initialpos=2, .mode=fsFromMark, .delta=1, .finalpos=3, .err=noErr},
		{.size=4, .initialpos=2, .mode=fsFromMark, .delta=2, .finalpos=4, .err=noErr},
		{.size=4, .initialpos=2, .mode=fsFromMark, .delta=3, .finalpos=4, .err=eofErr},
		{.size=4, .initialpos=2, .mode=fsFromMark, .delta=4, .finalpos=4, .err=eofErr},
		{.size=4, .initialpos=2, .mode=fsFromMark, .delta=5, .finalpos=4, .err=eofErr},
		{.size=4, .initialpos=3, .mode=fsFromMark, .delta=-5, .finalpos=3, .err=posErr},
		{.size=4, .initialpos=3, .mode=fsFromMark, .delta=-4, .finalpos=3, .err=posErr},
		{.size=4, .initialpos=3, .mode=fsFromMark, .delta=-3, .finalpos=0, .err=noErr},
		{.size=4, .initialpos=3, .mode=fsFromMark, .delta=-2, .finalpos=1, .err=noErr},
		{.size=4, .initialpos=3, .mode=fsFromMark, .delta=-1, .finalpos=2, .err=noErr},
		{.size=4, .initialpos=3, .mode=fsFromMark, .delta=0, .finalpos=3, .err=noErr},
		{.size=4, .initialpos=3, .mode=fsFromMark, .delta=1, .finalpos=4, .err=noErr},
		{.size=4, .initialpos=3, .mode=fsFromMark, .delta=2, .finalpos=4, .err=eofErr},
		{.size=4, .initialpos=3, .mode=fsFromMark, .delta=3, .finalpos=4, .err=eofErr},
		{.size=4, .initialpos=3, .mode=fsFromMark, .delta=4, .finalpos=4, .err=eofErr},
		{.size=4, .initialpos=3, .mode=fsFromMark, .delta=5, .finalpos=4, .err=eofErr},
		{.size=4, .initialpos=4, .mode=fsFromMark, .delta=-5, .finalpos=4, .err=posErr},
		{.size=4, .initialpos=4, .mode=fsFromMark, .delta=-4, .finalpos=0, .err=noErr},
		{.size=4, .initialpos=4, .mode=fsFromMark, .delta=-3, .finalpos=1, .err=noErr},
		{.size=4, .initialpos=4, .mode=fsFromMark, .delta=-2, .finalpos=2, .err=noErr},
		{.size=4, .initialpos=4, .mode=fsFromMark, .delta=-1, .finalpos=3, .err=noErr},
		{.size=4, .initialpos=4, .mode=fsFromMark, .delta=0, .finalpos=4, .err=noErr},
		{.size=4, .initialpos=4, .mode=fsFromMark, .delta=1, .finalpos=4, .err=eofErr},
		{.size=4, .initialpos=4, .mode=fsFromMark, .delta=2, .finalpos=4, .err=eofErr},
		{.size=4, .initialpos=4, .mode=fsFromMark, .delta=3, .finalpos=4, .err=eofErr},
		{.size=4, .initialpos=4, .mode=fsFromMark, .delta=4, .finalpos=4, .err=eofErr},
		{.size=4, .initialpos=4, .mode=fsFromMark, .delta=5, .finalpos=4, .err=eofErr},
		{-1}
	};

	for (struct line *l=lines; l->size!=-1; l++) {
		short ref = MkScratchFileAlphabetic(l->size);

		// Pre-set the mark
		struct IOParam setuppb = {.ioRefNum=ref, .ioPosMode=fsFromStart, .ioPosOffset=l->initialpos};
		PBSetFPosSync((void *)&setuppb);
		long thepos = 99;
		GetFPos(ref, &thepos);
		if (thepos != l->initialpos) TAPBailOut("Could not pre-set mark, wanted %d got %d", l->initialpos, thepos);

		struct IOParam pb = {.ioRefNum=ref, .ioPosMode=l->mode, .ioPosOffset=l->delta};
		PBSetFPosSync((void *)&pb);

		bool ok = pb.ioPosOffset==l->finalpos && pb.ioResult==l->err;
		TAPResult(ok, "SetFPos(size=%d, initialpos=%d, mode=%s, delta=%d) -> (finalpos=%d, err=%s)",
			l->size, l->initialpos, PosModeName(l->mode), l->delta, l->finalpos, ErrName(l->err));

		if (!ok) {
			printf("# got (finalpos=%d, err=%s)\n", (int)pb.ioPosOffset, ErrName(pb.ioResult));
		}

		thepos = 99;
		GetFPos(ref, &thepos);
		if (thepos != pb.ioPosOffset) TAPBailOut("FCB.fcbCrPs != PB.ioPosOffset");

		FSClose(ref);
		FSDelete("\pscratch", 0);
	}
}