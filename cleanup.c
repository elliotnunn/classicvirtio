/* Copyright (c) Elliot Nunn */
/* Licensed under the MIT license */

/* Hopefully the compiler will optimise this type-safe palaver down to 4 bytes of object code */

#include "cleanup.h"

enum {
	NOARG,
	VOIDPTR,
	CHARPTR,
};

int n;
static struct {
	union {
		void (*noarg)(void);
		void (*voidptr)(void *);
		void (*charptr)(char *);
	} func;
	union {
		void *voidptr;
		char *charptr;
	} arg;
	int type;
} funcs[16];

void RegisterCleanup(void (*function)(void)) {
	funcs[n].func.noarg = function;
	funcs[n].type = NOARG;
	n++;
}

void RegisterCleanupVoidPtr(void (*function)(void *), void *arg) {
	funcs[n].func.voidptr = function;
	funcs[n].arg.voidptr = arg;
	funcs[n].type = VOIDPTR;
	n++;
}

void RegisterCleanupCharPtr(void (*function)(char *), char *arg) {
	funcs[n].func.charptr = function;
	funcs[n].arg.charptr = arg;
	funcs[n].type = CHARPTR;
	n++;
}

void Cleanup(void) {
	for (int i=n-1; i>=0; i--) {
		if (funcs[i].type == NOARG) {
			funcs[i].func.noarg();
		} else if (funcs[i].type == VOIDPTR) {
			funcs[i].func.voidptr(funcs[i].arg.voidptr);
		} else if (funcs[i].type == CHARPTR) {
			funcs[i].func.charptr(funcs[i].arg.charptr);
		}
	}
	n = 0;
}
