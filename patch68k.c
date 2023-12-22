/* Copyright (c) 2023 Elliot Nunn */
/* Licensed under the MIT license */

#include <Gestalt.h>
#include <Memory.h>
#include <MixedMode.h>
#include <Patches.h>

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "callupp.h"
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
	if (vec & 0xffff0000) {
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
	if (vec & 0xffff0000) {
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

	*code++ = 0x48; // PEA code
	*code++ = 0x7a;
	short delta = block->code - code;
	*code++ = delta >> 8;
	*code++ = delta;

	void *unpatcher = STATICDESCRIPTOR(Unpatch68k,
		kPascalStackBased | STACK_ROUTINE_PARAMETER(1, kFourByteCode));

	*code++ = 0x4e; // JSR Unpatch68k
	*code++ = 0xb9;
	*code++ = (unsigned long)unpatcher >> 24;
	*code++ = (unsigned long)unpatcher >> 16;
	*code++ = (unsigned long)unpatcher >> 8;
	*code++ = (unsigned long)unpatcher;

	*code++ = 0x4c; // MOVEM.L (sp)+,d0-d2/a0-a2
	*code++ = 0xdf;
	*code++ = 0x07;
	*code++ = 0x07;

	*code++ = 0x4e; // RTS
	*code++ = 0x75;

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

pascal void Unpatch68k(void *patch) {
	struct block *block = patch - offsetof(struct block, code);

	if (getvec(block->vector) == &block->code) {
		// yay, we never got over-patched
		printf("Unpatched %X back to %p\n", block->vector, block->original);

		setvec(block->vector, block->original);
		DisposePtr((Ptr)block);
	} else {
		// someone over-patched us! yuck...
		if ((long)block->original == 0 || (long)block->original == -1) {
			// As always, never jump to a nonexistent address
			block->code[0] = 0x4e; // RTS
			block->code[1] = 0x75;
		} else {
			block->code[0] = 0x4e; // JMP
			block->code[1] = 0xb9;
			memcpy(block->code+2, &block->original, 4);
		}
		BlockMove(block, block, 14); // clear 68k emulator cache

		printf("Unpatched %#x using a thunk:\n   ", block->vector);
		for (unsigned char *i=block->code; i<block->code+6; i+=2) {
			printf(" %02x%02x", i[0], i[1]);
		}
		printf("\n");
	}
}
