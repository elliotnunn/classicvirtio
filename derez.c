/* Copyright (c) 2024 Elliot Nunn */
/* Licensed under the MIT license */

#include <string.h>

#include "9buf.h"
#include "9p.h"
#include "printf.h"
#include "panic.h"

#include "derez.h"

#define READ16BE(S) ((255 & (S)[0]) << 8 | (255 & (S)[1]))
#define READ24BE(S) \
  ((uint32_t)(255 & (S)[0]) << 16 | \
   (uint32_t)(255 & (S)[1]) << 8 | \
   (uint32_t)(255 & (S)[2]))
#define READ32BE(S) \
	 ((uint32_t)(255 & (S)[0]) << 24 | \
	  (uint32_t)(255 & (S)[1]) << 16 | \
	  (uint32_t)(255 & (S)[2]) << 8 | \
	  (uint32_t)(255 & (S)[3]))

static char *lutget(char *dest, const char *lut, char letter);
static char *derezHeader(char *p, uint8_t attrib, char *type, int16_t id, uint8_t *name);
static void derezFullLine(char *dest, char *src);

// Escape code lookup table for quoted strings
// (needs tweaking to switch between single and double quoted strings)
static const char lut[5*256] =
	"\\0x00"     "\\0x01"     "\\0x02"     "\\0x03"     "\\0x04"     "\\0x05"     "\\0x06"     "\\0x07"
	"\\b\0\0\0"  "\\t\0\0\0"  "\\r\0\0\0"  "\\v\0\0\0"  "\\f\0\0\0"  "\\n\0\0\0"  "\\0x0E"     "\\0x0F"
	"\\0x10"     "\\0x11"     "\\0x12"     "\\0x13"     "\\0x14"     "\\0x15"     "\\0x16"     "\\0x17"
	"\\0x18"     "\\0x19"     "\\0x1A"     "\\0x1B"     "\\0x1C"     "\\0x1D"     "\\0x1E"     "\\0x1F"
	" \0\0\0\0"  "!\0\0\0\0"  "\"\0\0\0\0" "#\0\0\0\0"  "$\0\0\0\0"  "%\0\0\0\0"  "&\0\0\0\0"  "'\0\0\0\0"
	"(\0\0\0\0"  ")\0\0\0\0"  "*\0\0\0\0"  "+\0\0\0\0"  ",\0\0\0\0"  "-\0\0\0\0"  ".\0\0\0\0"  "/\0\0\0\0"
	"0\0\0\0\0"  "1\0\0\0\0"  "2\0\0\0\0"  "3\0\0\0\0"  "4\0\0\0\0"  "5\0\0\0\0"  "6\0\0\0\0"  "7\0\0\0\0"
	"8\0\0\0\0"  "9\0\0\0\0"  ":\0\0\0\0"  ";\0\0\0\0"  "<\0\0\0\0"  "=\0\0\0\0"  ">\0\0\0\0"  "?\0\0\0\0"
	"@\0\0\0\0"  "A\0\0\0\0"  "B\0\0\0\0"  "C\0\0\0\0"  "D\0\0\0\0"  "E\0\0\0\0"  "F\0\0\0\0"  "G\0\0\0\0"
	"H\0\0\0\0"  "I\0\0\0\0"  "J\0\0\0\0"  "K\0\0\0\0"  "L\0\0\0\0"  "M\0\0\0\0"  "N\0\0\0\0"  "O\0\0\0\0"
	"P\0\0\0\0"  "Q\0\0\0\0"  "R\0\0\0\0"  "S\0\0\0\0"  "T\0\0\0\0"  "U\0\0\0\0"  "V\0\0\0\0"  "W\0\0\0\0"
	"X\0\0\0\0"  "Y\0\0\0\0"  "Z\0\0\0\0"  "[\0\0\0\0"  "\\\\\0\0\0" "]\0\0\0\0"  "^\0\0\0\0"  "_\0\0\0\0"
	"`\0\0\0\0"  "a\0\0\0\0"  "b\0\0\0\0"  "c\0\0\0\0"  "d\0\0\0\0"  "e\0\0\0\0"  "f\0\0\0\0"  "g\0\0\0\0"
	"h\0\0\0\0"  "i\0\0\0\0"  "j\0\0\0\0"  "k\0\0\0\0"  "l\0\0\0\0"  "m\0\0\0\0"  "n\0\0\0\0"  "o\0\0\0\0"
	"p\0\0\0\0"  "q\0\0\0\0"  "r\0\0\0\0"  "s\0\0\0\0"  "t\0\0\0\0"  "u\0\0\0\0"  "v\0\0\0\0"  "w\0\0\0\0"
	"x\0\0\0\0"  "y\0\0\0\0"  "z\0\0\0\0"  "{\0\0\0\0"  "|\0\0\0\0"  "}\0\0\0\0"  "~\0\0\0\0"  "\\?\0\0\0"
	"\\0x80"     "\\0x81"     "\\0x82"     "\\0x83"     "\\0x84"     "\\0x85"     "\\0x86"     "\\0x87"
	"\\0x88"     "\\0x89"     "\\0x8A"     "\\0x8B"     "\\0x8C"     "\\0x8D"     "\\0x8E"     "\\0x8F"
	"\\0x90"     "\\0x91"     "\\0x92"     "\\0x93"     "\\0x94"     "\\0x95"     "\\0x96"     "\\0x97"
	"\\0x98"     "\\0x99"     "\\0x9A"     "\\0x9B"     "\\0x9C"     "\\0x9D"     "\\0x9E"     "\\0x9F"
	"\\0xA0"     "\\0xA1"     "\\0xA2"     "\\0xA3"     "\\0xA4"     "\\0xA5"     "\\0xA6"     "\\0xA7"
	"\\0xA8"     "\\0xA9"     "\\0xAA"     "\\0xAB"     "\\0xAC"     "\\0xAD"     "\\0xAE"     "\\0xAF"
	"\\0xB0"     "\\0xB1"     "\\0xB2"     "\\0xB3"     "\\0xB4"     "\\0xB5"     "\\0xB6"     "\\0xB7"
	"\\0xB8"     "\\0xB9"     "\\0xBA"     "\\0xBB"     "\\0xBC"     "\\0xBD"     "\\0xBE"     "\\0xBF"
	"\\0xC0"     "\\0xC1"     "\\0xC2"     "\\0xC3"     "\\0xC4"     "\\0xC5"     "\\0xC6"     "\\0xC7"
	"\\0xC8"     "\\0xC9"     "\\0xCA"     "\\0xCB"     "\\0xCC"     "\\0xCD"     "\\0xCE"     "\\0xCF"
	"\\0xD0"     "\\0xD1"     "\\0xD2"     "\\0xD3"     "\\0xD4"     "\\0xD5"     "\\0xD6"     "\\0xD7"
	"\\0xD8"     "\\0xD9"     "\\0xDA"     "\\0xDB"     "\\0xDC"     "\\0xDD"     "\\0xDE"     "\\0xDF"
	"\\0xE0"     "\\0xE1"     "\\0xE2"     "\\0xE3"     "\\0xE4"     "\\0xE5"     "\\0xE6"     "\\0xE7"
	"\\0xE8"     "\\0xE9"     "\\0xEA"     "\\0xEB"     "\\0xEC"     "\\0xED"     "\\0xEE"     "\\0xEF"
	"\\0xF0"     "\\0xF1"     "\\0xF2"     "\\0xF3"     "\\0xF4"     "\\0xF5"     "\\0xF6"     "\\0xF7"
	"\\0xF8"     "\\0xF9"     "\\0xFA"     "\\0xFB"     "\\0xFC"     "\\0xFD"     "\\0xFE"     "\\0xFF";

void DeRez(uint32_t forkfid, uint32_t textfid) {
	uint32_t head[4];
	Read9(forkfid, head, 0, sizeof head, NULL);
	char map[64*1024];
	Read9(forkfid, map, head[1], head[3], NULL);

	char *tl = map + READ16BE(map+24);
	char *nl = map + READ16BE(map+26);

	int nt = (uint16_t)(READ16BE(tl) + 1);

	char rb[8*1024], wb[32*1024];
	SetRead(forkfid, rb, sizeof rb);
	SetWrite(textfid, wb, sizeof wb);

	char *src=NULL, *dst=NULL;
	for (int i=0; i<nt; i++) {
		char *t = tl + 2 + 8*i;
		int nr = READ16BE(t+4) + 1;
		int r1 = READ16BE(t+6);
		for (int j=0; j<nr; j++) {
			char *r = tl + r1 + 12*j;

			int16_t id = READ16BE(r);
			uint16_t nameoff = READ16BE(r+2);
			uint8_t *name = (nameoff==0xffff) ? NULL : (nl+nameoff);
			uint8_t attr = *(r+4);
			uint32_t contoff = READ24BE(r+5);

			dst = derezHeader(dst, attr, t, id, name);

			RSeek(head[0] + contoff);
			src = RBuffer(src, 4); // src always starts as NULL here
			uint32_t len = READ32BE(src);
			src += 4;

			// The fast part
			for (size_t i=0; i<len; i+=16) {
				src = RBuffer(src, 16);
				dst = WBuffer(dst, 78);
				derezFullLine(dst, src);
				src += 16;
				dst += 78;
			}
			src = RBuffer(src, 0); // to be clear: sets src to NULL

			int missing = (16 - (len % 16)) % 16;
			if (missing) { // edit the hastily generated last line
				int wipehex = missing*2 + missing/2;
				memset(dst-35-wipehex, ' ', wipehex);
				dst[-35-wipehex-1] = '"';

				dst = dst - missing - 4;
				*dst++ = ' ';
				*dst++ = '*';
				*dst++ = '/';
				*dst++ = '\n';
			}

			dst = WBuffer(dst, 4);
			*dst++ = '}';
			*dst++ = ';';
			*dst++ = '\n';
			*dst++ = '\n';
		}
	}
	dst = WBuffer(dst, 0); // definitively give-back the dest buffer
	WFlush();
}

static char *lutget(char *dest, const char *lut, char letter) {
	lut += 5 * (255 & letter);
	for (int i=0; i<5; i++) {
		if ((*dest = *lut++) == 0) break;
		dest++;
	}
	return dest;
}

static char *derezHeader(char *p, uint8_t attrib, char *type, int16_t id, uint8_t *name) {
	p = WBuffer(p, 2048);
	p = stpcpy(p, "data '");

	for (int i=0; i<4; i++) {
		p = lutget(p, lut, type[i]);
		if (p[-1] == '\'') p = stpcpy(p-1, "\\'"); // escape single quote
	}

	p += sprintf(p, "' (%d", id);

	if (name) {
		p = stpcpy(p, ", \"");
		for (uint8_t i=0; i<name[0]; i++) {
			p = lutget(p, lut, name[i+1]);
			if (p[-1] == '"') p = stpcpy(p-1, "\\\""); // escape double quote
		}
		*p++ = '"';
	}

	if (attrib & 0x83) {
		p += sprintf(p, ", $%02X", attrib);
	} else {
		if (attrib & 0x40) {
			p = stpcpy(p, ", sysheap");
		}
		if (attrib & 0x20) {
			p = stpcpy(p, ", purgeable");
		}
		if (attrib & 0x10) {
			p = stpcpy(p, ", locked");
		}
		if (attrib & 8) {
			p = stpcpy(p, ", protected");
		}
		if (attrib & 4) {
			p = stpcpy(p, ", preload");
		}
	}

	p = stpcpy(p, ") {\n");
	return p;
}

static char cmtLUT[256] =
	"................................"
	" !\"#$%&'()*+,-./0123456789:;<=>?"
	"@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_"
	"`abcdefghijklmnopqrstuvwxyz{|}~."
	"................................"
	"................................"
	"................................"
	"................................";

static const char hexLUT[16] = "0123456789ABCDEF";

static void derezFullLine(char *dest, char *src) {
	*dest++ = '\t';
	*dest++ = '$';
	*dest++ = '"';

	char *hexinc = src;
	*dest++ = hexLUT[15 & (*hexinc >> 4)];
	*dest++ = hexLUT[15 & *hexinc++];
	*dest++ = hexLUT[15 & (*hexinc >> 4)];
	*dest++ = hexLUT[15 & *hexinc++];
	*dest++ = ' ';
	*dest++ = hexLUT[15 & (*hexinc >> 4)];
	*dest++ = hexLUT[15 & *hexinc++];
	*dest++ = hexLUT[15 & (*hexinc >> 4)];
	*dest++ = hexLUT[15 & *hexinc++];
	*dest++ = ' ';
	*dest++ = hexLUT[15 & (*hexinc >> 4)];
	*dest++ = hexLUT[15 & *hexinc++];
	*dest++ = hexLUT[15 & (*hexinc >> 4)];
	*dest++ = hexLUT[15 & *hexinc++];
	*dest++ = ' ';
	*dest++ = hexLUT[15 & (*hexinc >> 4)];
	*dest++ = hexLUT[15 & *hexinc++];
	*dest++ = hexLUT[15 & (*hexinc >> 4)];
	*dest++ = hexLUT[15 & *hexinc++];
	*dest++ = ' ';
	*dest++ = hexLUT[15 & (*hexinc >> 4)];
	*dest++ = hexLUT[15 & *hexinc++];
	*dest++ = hexLUT[15 & (*hexinc >> 4)];
	*dest++ = hexLUT[15 & *hexinc++];
	*dest++ = ' ';
	*dest++ = hexLUT[15 & (*hexinc >> 4)];
	*dest++ = hexLUT[15 & *hexinc++];
	*dest++ = hexLUT[15 & (*hexinc >> 4)];
	*dest++ = hexLUT[15 & *hexinc++];
	*dest++ = ' ';
	*dest++ = hexLUT[15 & (*hexinc >> 4)];
	*dest++ = hexLUT[15 & *hexinc++];
	*dest++ = hexLUT[15 & (*hexinc >> 4)];
	*dest++ = hexLUT[15 & *hexinc++];
	*dest++ = ' ';
	*dest++ = hexLUT[15 & (*hexinc >> 4)];
	*dest++ = hexLUT[15 & *hexinc++];
	*dest++ = hexLUT[15 & (*hexinc >> 4)];
	*dest++ = hexLUT[15 & *hexinc++];

	*dest++ = '"';
	for (int i=0; i<12; i++) *dest++ = ' ';
	*dest++ = '/';
	*dest++ = '*';
	*dest++ = ' ';

	cmtLUT['/'] = '/';
	for (int i=0; i<16; i++) {
		char orig = src[i];
		char repl = cmtLUT[255 & orig];
		*dest++ = repl;
		if (orig == '*') {
			cmtLUT['/'] = '.';
		} else if ((255 & orig) >= 32) { // Rez quirk
			cmtLUT['/'] = '/';
		}
	}

	*dest++ = ' ';
	*dest++ = '*';
	*dest++ = '/';
	*dest++ = '\n';
}
