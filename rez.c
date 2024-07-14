/* Copyright (c) 2024 Elliot Nunn */
/* Licensed under the MIT license */

#include <stdlib.h>
#include <string.h>

#include "9buf.h"
#include "9p.h"
#include "panic.h"
#include "printf.h"

#include "rez.h"

struct res {
	uint32_t type;
	int16_t id;
	uint16_t nameoff;
	uint32_t attrandoff;
};

static long rezHeader(uint8_t *attrib, uint32_t *type, int16_t *id, bool *hasname, uint8_t name[256]);
static int rezBody(void);
static long quote(char *dest, char **src, char mark, int min, int max);
static long integer(char **src);
static int resorder(const void *a, const void *b);

static const char whitespace[256] = {[' ']=1, ['\n']=1, ['\r']=1, ['\t']=1};

static const uint16_t hexlutMS[256] = {
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0x0000, 0x0010, 0x0020, 0x0030, 0x0040, 0x0050, 0x0060, 0x0070,
	0x0080, 0x0090, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0x00a0, 0x00b0, 0x00c0, 0x00d0, 0x00e0, 0x00f0, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0x00a0, 0x00b0, 0x00c0, 0x00d0, 0x00e0, 0x00f0, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
};

static const uint16_t hexlutLS[256] = {
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006, 0x0007,
	0x0008, 0x0009, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0x000a, 0x000b, 0x000c, 0x000d, 0x000e, 0x000f, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0x000a, 0x000b, 0x000c, 0x000d, 0x000e, 0x000f, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
};

uint32_t Rez(uint32_t textfid, uint32_t forkfid) {
	int err;
	int nres = 0;
	struct res resources[2727]; // see Guide to the File System Manager v1.2 pD-3
	char namelist[0x8000];

	// sizes for pointer calculations
	size_t contentsize=0, namesize=0;

	// Hefty stack IO buffer, rededicate to reading after writes done
	enum {WB = 8*1024, RB = 32*1024};
	char buf[WB+RB];
	SetRead(textfid, buf+WB, RB);
	SetWrite(forkfid, buf, WB);

	char *b = WBuffer(NULL, 256);
	memset(b, 0, 256);
	b += 256;
	WBuffer(b, 0);

	// Slurp the Rez file sequentially, acquiring:
	// type/id/attrib/bodyoffset/nameoffset into resources array
	// name into namelist
	// actual data into scratch file
	for (;;) {
		struct res r;
		uint8_t attrib;
		bool hasname;
		unsigned char name[256];

		// Does not take much time at all
		long err = rezHeader(&attrib, &r.type, &r.id, &hasname, name);
		if (err == 0) {
			break; // EOF
		} else if (err != 1) {
			printf("header failure %.4s\n", &err);
			panic("header failure"); // might be recoverable in future
		}

		if (nres >= sizeof resources/sizeof *resources) {
			panic("too many resources in file");
		}

		int64_t lenheaderpos = WTell();
		r.attrandoff = (lenheaderpos - 256) | ((uint32_t)attrib << 24);

		char *b = WBuffer(NULL, 4);
		for (int i=0; i<4; i++) *b++ = 0;
		WBuffer(b, 0);

		if (rezBody()) panic("failed to read Rez body");

		uint32_t bodylen = WTell() - lenheaderpos - 4;
		Rewrite(&bodylen, lenheaderpos, 4);

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

		resources[nres++] = r;
	}
	contentsize = WTell() - 256;

	// We will no longer use the read buffer, rededicate it to write buffer
	// (actually this capability was removed, rethink how to do this)

	// Group resources of the same type together and count unique types
	qsort(resources, nres, sizeof *resources, resorder);
	int ntype = 0;
	for (int i=0; i<nres; i++) {
		if (i==0 || resources[i-1].type!=resources[i].type) ntype++;
	}

	// Resource map header
	b = WBuffer(NULL, 30);
	for (int i=0; i<25; i++) *b++ = 0;
	*b++ = 28; // offset to type list
	*b++ = (28 + 2 + 8*ntype + 12*nres) >> 8; // offset to name
	*b++ = (28 + 2 + 8*ntype + 12*nres) >> 0;
	*b++ = (ntype - 1) >> 8; // resource types in the map minus 1
	*b++ = (ntype - 1) >> 0;
	WBuffer(b, 0);

	// Resource type list
	int base = 2 + 8*ntype;
	int ott = 0;
	for (int i=0; i<nres; i++) {
		if (i==nres-1 || resources[i].type!=resources[i+1].type) {
			// last resource of this type
			char *b = WBuffer(NULL, 8);
			*b++ = resources[i].type >> 24;
			*b++ = resources[i].type >> 16;
			*b++ = resources[i].type >> 8;
			*b++ = resources[i].type >> 0;
			*b++ = ott >> 8;
			*b++ = ott >> 0;
			*b++ = base >> 8;
			*b++ = base >> 0;
			WBuffer(b, 0);
			base += 12 * (ott + 1);
			ott = 0;
		} else {
			ott++;
		}
	}

	// Resource reference list
	for (int i=0; i<nres; i++) {
		char *b = WBuffer(NULL, 12);
		*b++ = resources[i].id >> 8;
		*b++ = resources[i].id >> 0;
		*b++ = resources[i].nameoff >> 8;
		*b++ = resources[i].nameoff >> 0;
		*b++ = resources[i].attrandoff >> 24;
		*b++ = resources[i].attrandoff >> 16;
		*b++ = resources[i].attrandoff >> 8;
		*b++ = resources[i].attrandoff >> 0;
		for (int i=0; i<4; i++) *b++ = 0;
		WBuffer(b, 0);
	}

	uint32_t head[4] = {
		256,
		256+contentsize,
		contentsize,
		28+2+8*ntype+12*nres+namesize
	};
	Rewrite(head, 0, sizeof head);
	WFlush();

	// Does this strictly need to be a separate call?
	if (Write9(forkfid, namelist, WTell(), namesize, NULL)) {
		panic("failed to write name list");
	}

	panic("done writing, check it out");

	return 256+contentsize+28+2+8*ntype+12*nres+namesize;
}

// 0 = eof, 1 = good, else = error fourcc
static long rezHeader(uint8_t *attrib, uint32_t *type, int16_t *id, bool *hasname, uint8_t name[256]) {
	long err;
	char *recv = RBuffer(NULL, 2048);
	*attrib = 0;
	*hasname = false;
#define STRIPWS() {while (whitespace[255 & *recv++]); recv--;}

	STRIPWS();
	if (*recv == 0) return 0; // EOF
	else if (memcmp(recv, "data", 4)) return 'Hdta';
	recv += 4;
	STRIPWS();
	err = quote((char *)type, &recv, '\'', 4, 4);
	if (err > 255) return err;
	STRIPWS();
	if (*recv++ != '(') return 'Hno(';
	STRIPWS();
	long gotid = integer(&recv);
	if (gotid < -0x8000 || gotid > 0x7fff) return 'Hno#';
	*id = gotid;
	STRIPWS();
	if (*recv != ',') goto nocomma;
	recv++;
	STRIPWS();
	if (*recv != '"') goto namedone;
	*hasname = true;
	err = quote((char *)name+1, &recv, '"', 0, 255);
	if (err > 255) return err;
	name[0] = err;
	STRIPWS();
	if (*recv != ',') goto nocomma;
	recv++;
	STRIPWS();

namedone:
	if (*recv != '$') goto constants;
	recv++;
	uint16_t hex = hexlutMS[*recv++];
	hex |= hexlutLS[*recv++];
	if (hex & 0x8000) return 'Hbd$';
	*attrib = hex;
	STRIPWS();
	goto nocomma;

constants:
	if (!memcmp(recv, "sysheap", 7)) {
		recv += 7;
		*attrib |= 0x40;
	} else if (!memcmp(recv, "purgeable", 9)) {
		recv += 9;
		*attrib |= 0x20;
	} else if (!memcmp(recv, "locked", 6)) {
		recv += 6;
		*attrib |= 0x10;
	} else if (!memcmp(recv, "protected", 9)) {
		recv += 9;
		*attrib |= 0x08;
	} else if (!memcmp(recv, "preload", 7)) {
		recv += 7;
		*attrib |= 0x04;
	}
	STRIPWS();
	if (*recv != ',') goto nocomma;
	recv++;
	STRIPWS();
	goto constants;

nocomma:
	if (*recv++ != ')') return 'Hno)';

	RBuffer(recv, 0);
	return 1;
}

// critically important for speed
// return nonzero on error
static int rezBody(void) {
	char digit1, digit2;

	char *recv = RBuffer(NULL, 1024);
	char *send = WBuffer(NULL, 512);

	while (whitespace[255 & *recv++]); recv--;
	if (*recv++ != '{') return -1001;

	stem:
	while (whitespace[255 & *recv++]); recv--;
	switch (*recv++) {
	case '/':
		goto comment;
	case '$':
		recv = RBuffer(recv, 1024);
		send = WBuffer(send, 512);
		goto hexquote;
	case '}':
		goto end;
	case 0:
		panic("unexpected EOF");
	default:
		panic("unexpected char");
	}

	comment:
	if (*recv++ != '*') panic("unexpected non-star");
	findstar:
	while (*recv++ != '*') {}
	while (*recv++ == '*') {}
	if (recv[-1] == '/') goto stem;
	else goto findstar;

	hexquote:
	if (*recv++ != '"') panic("unexpected non-quote");
	hex:
	while ((digit1 = *recv++) == ' ') {}
	digit2 = *recv++;
	int16_t val = hexlutMS[digit1] | hexlutLS[digit2];
	if (val < 0) {
		recv -= 2; // those weren't paired hex digits
		if (*recv++ != '"') {
			printf("bad hex <%.20s>\n", recv-20);
			panic("bad hex");
		}
		goto stem;
	}
	*send++ = val;
	goto hex;

	end:
	switch (*recv++) {
	case ' ': case '\t': case '\r': case '\n':
		goto end;
	case ';':
		goto realend;
	default:
		panic("unexpected after end-brace");
	}

	realend:
	RBuffer(recv, 0); // relinquish
	WBuffer(send, 0);

	return 0;
}

// return the number of chars or a much larger error code
static long quote(char *dest, char **src, char mark, int min, int max) {
	char *s = *src; // need to set this ptr back before returning (if success anyhow)
	int cnt = 0;
	if (*s++ != mark) return 'gon\0' | mark;

	for (;;) {
		char ch = *s++;
		if (ch == mark) break; // end of string
		else if (cnt == max) return 'mny\0' | mark; // too many chars already
		else if (ch != '\\') {
			dest[cnt++] = ch; // non-escaped char
			continue;
		}

		// escaped char
		ch = *s++;
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
			if (*s++ != 'x') return 'esc\0' | mark;
			uint16_t hex = hexlutMS[*s++];
			hex |= hexlutMS[*s++];
			if (hex & 0x8000) return 'hex\0' | mark;
			ch = hex;
		}
		dest[cnt++] = ch; // take it literally
	}
	if (cnt < min) return 'few\0' | mark;

	*src = s; // advance the ptr
	return cnt;
}

static long integer(char **src) {
	char *s = *src;
	long mag = 0;
	int sgn = 1;
	int ok = 0;
	if (*s == '-') {
		s++;
		sgn = -1;
	}
	for (;;) {
		int ch = *s;
		if (ch >= '0' && ch <= '9') {
			mag = 10*mag + ch-'0';
			ok = 1;
			s++;
		} else if (!ok || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')) {
			return -1; // bad token
		} else {
			*src = s;
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
