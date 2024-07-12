/* Copyright (c) 2024 Elliot Nunn */
/* Licensed under the MIT license */

#include <stdlib.h>
#include <string.h>

#include "9buf.h"
#include "panic.h"
#include "printf.h"

#include "rez.h"

struct res {
	uint32_t type;
	int16_t id;
	uint16_t nameoff;
	uint32_t attrandoff;
};

static int rezHeader(uint8_t *attrib, uint32_t *type, int16_t *id, bool *hasname, uint8_t name[256]);
static int32_t rezBody(void);
static bool isword(const char *word);
static void wspace();
static int quote(char mark, void *dest, int max);
static int hex(char ch);
static long integer(void);
static int resorder(const void *a, const void *b);

uint32_t Rez(uint32_t textfid, uint32_t forkfid) {
	int err;
	int nres = 0;
	struct res resources[2727]; // see Guide to the File System Manager v1.2 pD-3
	char namelist[0x10000];

	// sizes for pointer calculations
	size_t contentsize=0, namesize=0;

	// Hefty stack IO buffer, rededicate to reading after writes done
	enum {WB = 8*1024, RB = 32*1024};
	char buf[WB+RB];
	SetWrite(forkfid, buf, WB);
	SetRead(textfid, buf+WB, RB);
	wbufat = wbufseek = 256;

	// Slurp the Rez file sequentially, acquiring:
	// type/id/attrib/bodyoffset/nameoffset into resources array
	// name into namelist
	// actual data into scratch file
	for (;;) {
		struct res r;
		uint8_t attrib;
		bool hasname;
		unsigned char name[256];

		if (rezHeader(&attrib, &r.type, &r.id, &hasname, name) != 0)
			break;

		int32_t rezoffset = rbufseek;

		int32_t bodylen;
		int64_t fixup = wbufseek;
		for (int i=0; i<4; i++) Write(0);
		bodylen = rezBody();
		Overwrite(&bodylen, fixup, 4);
		if (bodylen < 0) panic("failed to read Rez body");

		if (nres >= sizeof resources/sizeof *resources) {
			panic("too many resources in file");
		}

		// append the name to the packed name list
		if (hasname) {
			if (namesize + 1 + name[0] > 0x10000)
				panic("filled name buffer");
			r.nameoff = namesize;
			memcpy(namelist + namesize, name, 1 + name[0]);
			namesize += 1 + name[0];
		} else {
			r.nameoff = 0xffff;
		}

		r.attrandoff = contentsize | ((uint32_t)attrib << 24);

		resources[nres++] = r;
		contentsize += 4 + bodylen;
	}

	// We will no longer use the read buffer, rededicate it to write buffer
	wbufsize = WB + RB;

	// Group resources of the same type together and count unique types
	qsort(resources, nres, sizeof *resources, resorder);
	int ntype = 0;
	for (int i=0; i<nres; i++) {
		if (i==0 || resources[i-1].type!=resources[i].type) ntype++;
	}

	// Resource map header
	for (int i=0; i<25; i++) Write(0);
	Write(28); // offset to type list
	Write((28 + 2 + 8*ntype + 12*nres) >> 8); // offset to name
	Write((28 + 2 + 8*ntype + 12*nres) >> 0);
	Write((ntype - 1) >> 8); // resource types in the map minus 1
	Write((ntype - 1) >> 0);

	// Resource type list
	int base = 2 + 8*ntype;
	int ott = 0;
	for (int i=0; i<nres; i++) {
		if (i==nres-1 || resources[i].type!=resources[i+1].type) {
			// last resource of this type
			Write(resources[i].type >> 24);
			Write(resources[i].type >> 16);
			Write(resources[i].type >> 8);
			Write(resources[i].type >> 0);
			Write(ott >> 8);
			Write(ott >> 0);
			Write(base >> 8);
			Write(base >> 0);
			base += 12 * (ott + 1);
			ott = 0;
		} else {
			ott++;
		}
	}

	// Resource reference list
	for (int i=0; i<nres; i++) {
		Write(resources[i].id >> 8);
		Write(resources[i].id >> 0);
		Write(resources[i].nameoff >> 8);
		Write(resources[i].nameoff >> 0);
		Write(resources[i].attrandoff >> 24);
		Write(resources[i].attrandoff >> 16);
		Write(resources[i].attrandoff >> 8);
		Write(resources[i].attrandoff >> 0);
		for (int i=0; i<4; i++) Write(0);
	}

	for (int i=0; i<namesize; i++) {
		Write(namelist[i]);
	}

	Flush();

	uint32_t head[4] = {
		256,
		256+contentsize,
		contentsize,
		28+2+8*ntype+12*nres+namesize
	};

	Write9(forkfid, head, 0, sizeof head, NULL);

	return 256+contentsize+28+2+8*ntype+12*nres+namesize;
}

// zero is good, negatives are errors
static int rezHeader(uint8_t *attrib, uint32_t *type, int16_t *id, bool *hasname, uint8_t name[256]) {
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
static int32_t rezBody(void) {
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
					Write(pendhex);

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
			ch = 8;
		} else if (ch == 't') {
			ch = 9;
		} else if (ch == 'r') {
			ch = 10;
		} else if (ch == 'v') {
			ch = 11;
		} else if (ch == 'f') {
			ch = 12;
		} else if (ch == 'n') {
			ch = 13;
		} else if (ch == '?') {
			ch = 127;
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

static int resorder(const void *a, const void *b) {
	const struct res *aa = a, *bb = b;

	if (aa->type > bb->type) {
		return 1;
	} else if (aa->type == bb->type) {
		if (aa->id > bb->id) {
			return 1;
		} else {
			return -1;
		}
	} else {
		return -1;
	}
}
