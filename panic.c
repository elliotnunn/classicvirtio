/* Copyright (c) 2023 Elliot Nunn */
/* Licensed under the MIT license */

#include <Debugging.h>
#include <string.h>

#include "printf.h"

#include "panic.h"

void panic(const char *panicstr) {
	printf("\npanic: %s\n", panicstr);
	char pstring[257];
	strcpy(pstring+1, panicstr);
	pstring[0] = strlen(pstring+1);
	DebugStr(pstring);
}
