/* Copyright (c) 2023 Elliot Nunn */
/* Licensed under the MIT license */

#include "printf.h"

#include <Debugging.h>

#include "panic.h"

void panic(const char *panicstr) {
	logenable = 1;
	logprefix[0] = 0;

	printf("\npanic: %s\n", panicstr);

	unsigned char pstring[256] = {0};
	for (int i=0; i<255; i++) {
		if (panicstr[i]) {
			pstring[i+1] = panicstr[i];
			pstring[0] = i+1;
		} else {
			break;
		}
	}
	DebugStr(pstring);
	for (;;) *(volatile char *)0x68f168f1 = 1;
}
