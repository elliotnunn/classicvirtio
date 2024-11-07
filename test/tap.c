/* Copyright (c) Elliot Nunn */
/* Licensed under the MIT license */

/*
Test Anything Protocol routines
*/

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "tap.h"

static int counter;

bool TAPResult(bool ok, const char *fmt, ...) {
	printf((ok) ? "ok %d - " : "not ok %d - ", ++counter);
	va_list args;
	va_start(args, fmt);
	vprintf(fmt, args);
	va_end(args);
	putc('\n', stdout);
	return ok;
}

void TAPBailOut(const char *fmt, ...) {
	fputs("Bail out! ", stdout);
	va_list args;
	va_start(args, fmt);
	vprintf(fmt, args);
	va_end(args);
	putc('\n', stdout);
	exit(1);
}

// run at end
void TAPPlan(void) {
	printf("1..%d\n", counter);
}
