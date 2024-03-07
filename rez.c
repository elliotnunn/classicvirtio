/* Copyright (c) 2024 Elliot Nunn */
/* Licensed under the MIT license */

#include "9buf.h"
#include "panic.h"
#include "printf.h"

#include "rez.h"

#include <string.h>

static bool isword(const char *word);
static void wspace();
static int quote(char mark, void *dest, int max);
static int hex(char ch);
static long integer(void);

// zero is good, negatives are errors
int RezHeader(uint8_t *attrib, uint32_t *type, int16_t *id, bool *hasname, uint8_t name[256]) {
	*attrib = 0;
	*hasname = false;

	wspace();

	if (!isword("data")) return -1;

	wspace();

	if (quote('\'', type, 4) != 4) return -2;

	wspace();

	if (Read() != '(') return -3;

	wspace();

	long gotid = integer();
	if (gotid < -0x8000 || gotid > 0x7fff) return -4;
	*id = gotid;

	wspace();
	if (Peek() != ',') goto nocomma;
	Read();
	wspace();

	if (Peek() != '"') goto namedone;
	*hasname = true;
	name[0] = quote('"', name+1, 255);

	wspace();
	if (Peek() != ',') goto nocomma;
	Read();
	wspace();

namedone:
	if (Peek() != '$') goto constants;
	Read();
	if (hex(Peek()) < 0) return -5;
	*attrib = hex(Read()) << 4;
	if (hex(Peek()) < 0) return -6;
	*attrib |= hex(Read());
	wspace();
	goto nocomma;

constants:
	if (Peek() == 's') {
		Read();
		if (!isword("ysheap")) return -7;
		*attrib |= 0x40;
	} else if (Peek() == 'l') {
		Read();
		if (!isword("ocked")) return -8;
		*attrib |= 0x10;
	} else if (Peek() == 'p') {
		Read();
		if (Peek() == 'u') {
			Read();
			if (!isword("rgeable")) return -9;
			*attrib |= 0x20;
		} else if (Peek() == 'r') {
			Read();
			if (Peek() == 'o') {
				Read();
				if (!isword("tected")) return -10;
				*attrib |= 0x08;
			} else if (Peek() == 'e') {
				Read();
				if (!isword("load")) return -11;
				*attrib |= 0x04;
			} else {
				return -12;
			}
		} else {
			return -13;
		}
	} else {
		return -14;
	}

	wspace();
	if (Peek() != ',') goto nocomma;
	Read();
	wspace();
	goto constants;

nocomma:
	if (Read() != ')') return -15;

	return 0;
}

// return number of resource bytes on success, or negative on error (e.g. -EIO)
int32_t RezBody(void) {
	int32_t got = 0;

	wspace();
	if (!ReadIf('{')) return -1001;

	for (;;) {
		wspace();

		int x = Read();
		if (x == '}') break;
		else if (x != '$') return -1002;

		if (!ReadIf('"')) return -1003;

		int pendhex = 1;
		for (;;) {
			int y = Read();
			if (hex(y)>=0) {
				pendhex = (pendhex << 4) | hex(y);
				if (pendhex & 0x100) {
					int err = Write(pendhex);
					if (err) return -err;

					got++;
					pendhex = 1;
				}
			} else if (y==' ' || y=='\t') {
				// ignore whitespace
			} else if (y=='"') {
				break;
			} else {
				return -1004;
			}
		}
	}

	wspace();
	if (!ReadIf(';')) return -1005;

	return got;
}

static bool isword(const char *word) {
	while (*word) {
		if (Read() != *word++) {
			return false;
		}
	}
	return true;
}

// Supports /*comments*/
// a hack for simplicity: a single / is treated as whitespace
static void wspace() {
	for (;;) {
		char ch = Peek();
		if (ch==' ' || ch=='\n' || ch=='\r' || ch=='\t' || ch=='\v' || ch=='\f') {
			Read();
		} else if (ch=='/') {
			Read();
			if (!ReadIf('*')) return;

			int last = 0;
			for (;;) {
				ch = Read();
				if (ch<0) return; // eof
				if (last=='*' && ch=='/') break;
				last = ch;
			}
		} else {
			return;
		}
	}
}

static int quote(char mark, void *dest, int max) {
	int cnt = 0;
	if (!ReadIf(mark)) return -1;

	for (;;) {
		char ch = Read();
		if (ch == mark) return cnt; // end of string
		if (cnt == max) return -1; // too many chars already
		if (ch != '\\') {
			((char *)dest)[cnt++] = ch; // non-escaped char
			continue;
		}

		// escaped char
		ch = Read();
		if (ch == 'b') {
			ch == 8;
		} else if (ch == 't') {
			ch == 9;
		} else if (ch == 'r') {
			ch == 10;
		} else if (ch == 'v') {
			ch == 11;
		} else if (ch == 'f') {
			ch == 12;
		} else if (ch == 'n') {
			ch == 13;
		} else if (ch == '?') {
			ch == 127;
		} else if (ch == '0') {
			if (Read() != 'x') return -1;
			ch = 0;
			for (int i=0; i<2; i++) {
				ch <<= 4;
				char hex = Read();
				if (hex >= '0' && hex <= '9') ch |= hex - '0';
				else if (hex >= 'a' && hex <= 'f') ch |= hex - 'a' + 16;
				else if (hex >= 'A' && hex <= 'F') ch |= hex - 'A' + 16;
				else return -1;
			}
		}

		((char *)dest)[cnt++] = ch;
	}
}

static int hex(char ch) {
	if (ch >= '0' && ch <= '9') {
		return ch - '0';
	} else if (ch >= 'a' && ch <= 'f') {
		return ch - 'a' + 10;
	} else if (ch >= 'A' && ch <= 'F') {
		return ch - 'A' + 10;
	} else {
		return -1;
	}
}

static long integer(void) {
	long mag = 0;
	int sgn = 1;
	int ok = 0;
	if (ReadIf('-')) {
		sgn = -1;
	}
	for (;;) {
		int ch = Peek();
		if (ch >= '0' && ch <= '9') {
			mag = 10*mag + ch-'0';
			ok = 1;
			Read();
		} else if (!ok || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')) {
			return 0x400000; // secret error code
		} else {
			return mag * sgn;
		}
	}
}
