/* Copyright (c) Elliot Nunn */
/* Licensed under the MIT license */

/*
A minimal 68k test application:
- run File Manager tests on the filesystem containing the app
- print output to testresult.txt in Test Anything Protocol format
*/

#include <stdio.h>

#include "tap.h"

// Dump the prototypes here, save on header files
void testSetFPos(void);
void testRead(void);
void testWrite(void);

int main(void) {
	freopen("testresult.txt", "w", stdout);

	testSetFPos();
	testRead();
	testWrite();

	TAPPlan();
	return 0;
}
