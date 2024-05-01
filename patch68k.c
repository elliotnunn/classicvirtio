/* Copyright (c) 2023 Elliot Nunn */
/* Licensed under the MIT license */

#include <Gestalt.h>
#include <Memory.h>
#include <Patches.h>

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "printf.h"

#include "patch68k.h"

struct block {
	void *original;
	long vector;
	unsigned char code[128];
};

static int hex(char c) {
	if ('0'<=c && c<='9') return c - '0';
	if ('a'<=c && c<='f') return c - 'a' + 10;
	return -1;
}

static void *getvec(long vec) {
	if (vec == 0) {
		return NULL;
	} if (vec & 0xffff0000) {
		return NULL; // GetGestaltProcPtr is sometimes unavailable
	} else if ((vec & 0xa800) == 0xa800) {
		return GetToolTrapAddress(vec);
	} else if ((vec & 0xa800) == 0xa000) {
		return GetOSTrapAddress(vec);
	} else {
		return *(void **)vec;
	}
}

static void setvec(long vec, void *addr) {
	if (vec == 0) {
		// no nothing
	} if (vec & 0xffff0000) {
		SelectorFunctionUPP old;
		if (NewGestalt(vec, addr) != noErr)
			ReplaceGestalt(vec, addr, &old);
	} else if ((vec & 0xa800) == 0xa800) {
		SetToolTrapAddress(addr, vec);
	} else if ((vec & 0xa800) == 0xa000) {
		SetOSTrapAddress(addr, vec);
	} else {
		*(void **)vec = addr;
	}
}

void *Patch68k(unsigned long vector, const char *fmt, ...) {
	va_list argp;
	va_start(argp, fmt);

	// if this allocation fails then the system is crashed anyway
	struct block *block = (struct block *)NewPtrSysClear(sizeof (struct block));

	block->vector = vector;
	block->original = getvec(vector);

	unsigned char *code = block->code;
	int midhex = 0;

	int labels[26] = {};
	int fixups[64];
	int nfixups = 0;

	for (const char *i=fmt; *i!=0;) {
		if (*i == '%') {
			i++;
			if (*i == 'b') {
				i++;
				*code++ = va_arg(argp, int);
			} else if (*i == 'w') {
				i++;
				short word = va_arg(argp, int);
				*code++ = word >> 8;
				*code++ = word;
			} else if (*i == 'l') {
				i++;
				long lword = va_arg(argp, long);
				*code++ = lword >> 24;
				*code++ = lword >> 16;
				*code++ = lword >> 8;
				*code++ = lword;
			} else if (*i == 'o') {
				i++;
				long lword = (long)block->original;

				// Special case: never JMP or JSR to NULL or 0xffffffff
				if (lword == -1 || lword == 0) {
					if (code[-2] == 0x4e && code[-1] == 0xf9) { // JMP
						code[-2] = 0x4e; // becomes RTS
						code[-1] = 0x75;
					} else if (code[-2] == 0x4e && code[-1] == 0xb9) { // JSR
						code[-2] = 0x60; // becomes BRA.S .+6
						code[-1] = 0x04;
					}
				}

				*code++ = lword >> 24;
				*code++ = lword >> 16;
				*code++ = lword >> 8;
				*code++ = lword;
			} else if (*i >= 'A' && *i <= 'Z') {
				fixups[nfixups++] = code - block->code;
				*code++ = *i;
				if ((code - block->code) & 1) code++;
				i++;
				while (*i >= 'A' && *i <= 'Z') i++; // ignore extra letters
			}
		} else if (*i >= 'A' && *i <= 'Z') {
			labels[*i-'A'] = code - block->code;
			i++;
			while (*i >= 'A' && *i <= 'Z') i++; // ignore extra letters
		} else if (hex(*i) != -1) {
			if (midhex) {
				*code++ |= hex(*i);
			} else {
				*code = hex(*i) << 4;
			}
			midhex = !midhex;
			i++;
		} else {
			i++; // ignore other letters
		}
	}

	va_end(argp);

	for (int i=0; i<nfixups; i++) {
		unsigned char *caller = block->code + fixups[i];
		unsigned char *callee = block->code + labels[*caller-'A'];
		if (fixups[i] % 2) {
			// At odd offsets, the off-by-one offset byte in a bra.s
			*caller = (signed char)(callee - caller - 1);
		} else {
			// At even offsets, the two-byte offset in lea, bra etc
			*(short *)caller = (signed short)(callee - caller);
		}
	}

	// Fallthrough code to remove the patch
	*code++ = 0x48; // MOVEM.L d0-d2/a0-a2,-(sp)
	*code++ = 0xe7;
	*code++ = 0xe0;
	*code++ = 0xe0;

	*code++ = 0x41; // LEA copycode,a0
	*code++ = 0xfa;
	*code++ = 0x00;
	*code++ = 0x10;

	*code++ = 0x43; // LEA entry,a1
	*code++ = 0xfa;
	short delta = block->code - code;
	*code++ = delta >> 8;
	*code++ = delta;

	*code++ = 0x70; // MOVEQ.L #12,d0
	*code++ = 0x0c;

	*code++ = 0xa0; // _BlockMove
	*code++ = 0x2e;

	*code++ = 0x4c; // MOVEM.L (sp)+,d0-d2/a0-a2
	*code++ = 0xdf;
	*code++ = 0x07;
	*code++ = 0x07;

	*code++ = 0x4e; // RTS
	*code++ = 0x75;

	// COPYCODE: (12 bytes to force BlockMove to clear the icache)
	if (block->original) {
		*code++ = 0x4e; // JMP
		*code++ = 0xf9;
		*code++ = (unsigned long)block->original >> 24;
		*code++ = (unsigned long)block->original >> 16;
		*code++ = (unsigned long)block->original >> 8;
		*code++ = (unsigned long)block->original;
	} else {
		*code++ = 0x4e; // RTS
		*code++ = 0x75;
	}
	code += 10; // at least 12 bytes for BlockMove to block-move

	BlockMove(block, block, sizeof *block); // clear 68k emulator cache
	SetPtrSize((Ptr)block, (char *)code - (char *)block); // shrink
	setvec(vector, &block->code); // install

	if (logenable) {
		printf("vector=%X, oldcode=%p, newcode=%p\n",
			vector, block->original, &block->code);

		int cnt = code - block->code;
		for (int i=0; i<cnt; i+=2) {
			printf("%s%04x%c",
				(i%16) ? "" : "    ",
				0xffff & *(uint16_t *)(block->code + i),
				(i==cnt-2 || (i%16)==14) ? '\n' : ' ');
		}
	}

	return &block->code;
}
