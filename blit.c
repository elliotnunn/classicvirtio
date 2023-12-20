/* Copyright (c) 2023 Elliot Nunn */
/* Licensed under the MIT license */

#include <stddef.h>

#include "blit.h"

const char BlitterAlign[6] = {
	4, // 1-bit
	4, // 2-bit
	4, // 4-bit
	1, // 8-bit
	2, // 16-bit
	4, // 32-bit
};

void Blit(int bppshift,
	short t, short *l, short b, short *r, const void *src, void *dest, long rowbytes,
	uint32_t clut[256], uint8_t red[256], uint8_t grn[256], uint8_t blu[256]) {

	long bytealign = BlitterAlign[bppshift];
	long pixalign = bytealign << 3 >> bppshift;
	long rowbytes_dest = rowbytes << (5 - bppshift);

	*l &= -pixalign;
	*r = (*r + pixalign - 1) & -pixalign;

	if (bppshift == 0) {
		int leftBytes = *l / 8;
		int rightBytes = *r / 8;
		uint32_t c0 = clut[0], c1 = clut[1];
		for (short y=t; y<b; y++) {
			uint32_t *srcctr = (void *)((char *)src + y * rowbytes + leftBytes);
			uint32_t *destctr = (void *)((char *)dest + y * rowbytes_dest + *l * 4);
			for (short x=leftBytes; x<rightBytes; x+=4) {
				uint32_t s = *srcctr++;
				for (int i=0; i<32; i++) {
					*destctr++ = (s & 0x80000000) ? c1 : c0;
					s <<= 1;
				}
			}
		}
	} else if (bppshift == 1) {
		int leftBytes = *l / 4;
		int rightBytes = *r / 4;
		for (short y=t; y<b; y++) {
			uint32_t *srcctr = (void *)((char *)src + y * rowbytes + leftBytes);
			uint32_t *destctr = (void *)((char *)dest + y * rowbytes_dest + *l * 4);
			for (short x=leftBytes; x<rightBytes; x+=4) {
				uint32_t s = *srcctr++;
				*destctr++ = clut[(s >> 30) & 3];
				*destctr++ = clut[(s >> 28) & 3];
				*destctr++ = clut[(s >> 26) & 3];
				*destctr++ = clut[(s >> 24) & 3];
				*destctr++ = clut[(s >> 22) & 3];
				*destctr++ = clut[(s >> 20) & 3];
				*destctr++ = clut[(s >> 18) & 3];
				*destctr++ = clut[(s >> 16) & 3];
				*destctr++ = clut[(s >> 14) & 3];
				*destctr++ = clut[(s >> 12) & 3];
				*destctr++ = clut[(s >> 10) & 3];
				*destctr++ = clut[(s >> 8) & 3];
				*destctr++ = clut[(s >> 6) & 3];
				*destctr++ = clut[(s >> 4) & 3];
				*destctr++ = clut[(s >> 2) & 3];
				*destctr++ = clut[(s >> 0) & 3];
			}
		}
	} else if (bppshift == 2) {
		int leftBytes = *l / 2;
		int rightBytes = *r / 2;
		for (short y=t; y<b; y++) {
			uint32_t *srcctr = (void *)((char *)src + y * rowbytes + leftBytes);
			uint32_t *destctr = (void *)((char *)dest + y * rowbytes_dest + *l * 4);
			for (short x=leftBytes; x<rightBytes; x+=4) {
				uint32_t s = *srcctr++;
				*destctr++ = clut[(s >> 28) & 15];
				*destctr++ = clut[(s >> 24) & 15];
				*destctr++ = clut[(s >> 20) & 15];
				*destctr++ = clut[(s >> 16) & 15];
				*destctr++ = clut[(s >> 12) & 15];
				*destctr++ = clut[(s >> 8) & 15];
				*destctr++ = clut[(s >> 4) & 15];
				*destctr++ = clut[(s >> 0) & 15];
			}
		}
	} else if (bppshift == 3) {
		for (short y=t; y<b; y++) {
			uint8_t *srcctr = (void *)((char *)src + y * rowbytes + *l);
			uint32_t *destctr = (void *)((char *)dest + y * rowbytes_dest + *l * 4);
			for (short x=*l; x<*r; x++) {
				*destctr++ = clut[*srcctr++];
			}
		}
	} else if (bppshift == 4) {
		for (short y=t; y<b; y++) {
			uint16_t *srcctr = (void *)((char *)src + y * rowbytes + *l * 2);
			uint32_t *destctr = (void *)((char *)dest + y * rowbytes_dest + *l * 4);
			for (short x=*l; x<*r; x++) {
				uint16_t s = *srcctr++;
				*destctr++ =
					((uint32_t)blu[((s & 0x1f) << 3) | ((s & 0x1f) << 3 >> 5)] << 24) |
					((uint32_t)grn[((s & 0x3e0) >> 5 << 3) | ((s & 0x3e0) >> 5 << 3 >> 5)] << 16) |
					((uint32_t)red[((s & 0x7c00) >> 10 << 3) | ((s & 0x7c00) >> 10 << 3 >> 5)] << 8);
			}
		}
	} else if (bppshift == 5) {
		for (short y=t; y<b; y++) {
			uint32_t *srcctr = (void *)((char *)src + y * rowbytes + *l * 4);
			uint32_t *destctr = (void *)((char *)dest + y * rowbytes_dest + *l * 4);
			for (short x=*l; x<*r; x++) {
				uint32_t s = *srcctr++;
				*destctr++ =
					((uint32_t)blu[s & 0xff] << 24) |
					((uint32_t)grn[(s >> 8) & 0xff] << 16) |
					((uint32_t)red[(s >> 16) & 0xff] << 8);
			}
		}
	}
}
