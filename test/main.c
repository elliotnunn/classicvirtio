/* Copyright (c) Elliot Nunn */
/* Licensed under the MIT license */

/*
A minimal 68k test application:
- run File Manager tests on the filesystem containing the app
- print output to testresult.txt in Test Anything Protocol format
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ShutDown.h>

#include "../extralowmem.h"
#include "scratch.h"
#include "tap.h"
#include "traptest.h"

// Dump the prototypes here, save on header files
void testNavigation(void);
void testSetFPos(void);
void testRead(void);
void testWrite(void);
void testOpenPerms(void);

static void shutDownIfOnlyApp(void) {
	fflush(stdout);
	fclose(stdout);
	if (!memcmp(XLMGetFinderName(), XLMGetCurApName(), XLMGetCurApName()[0]+1)) {
		ShutDwnPower();
	}
}

int main(void) {
	atexit(shutDownIfOnlyApp);
	freopen("testresult.txt", "w", stdout);
	InitScratch();

	testNavigation();
	testSetFPos();
	testRead();
	testWrite();
	testOpenPerms();

	TAPPlan();
	return 0;
}
