/* Copyright (c) Elliot Nunn */
/* Licensed under the MIT license */

// Missing accessors from Apple's LowMem.h
// I was prompted to write this by GCC bug 104828
// "Wrong out-of-bounds array access warning on literal pointers"

// Potential added advantages:
// Hide a null-pointer access from the compiler
// Split unaligned 32-bit accesses to two 16-bit accesses on PowerPC

#pragma once

#if GENERATINGCFM // PowerPC
#define MAKE_LM_ACCESSOR_STRING(address, type, name) \
	static inline const type XLMGet##name(void) { \
		const type ret; \
		asm volatile ("/*XLMGet" #name "*/ li %[reg]," #address "\n" : [reg] "=r" (ret)); \
		return ret; \
	}
#define MAKE_LM_ACCESSOR(address, type, name) \
	static inline type XLMGet##name(void) { \
		type ret; \
		if (sizeof (type) == 1) { \
			asm volatile ("/*XLMGet" #name "*/ lbz %[reg]," #address "(0)\n" : [reg] "=r" (ret)); \
		} else if (sizeof (type) == 2) { \
			asm volatile ("/*XLMGet" #name "*/ lhz %[reg]," #address "(0)\n" : [reg] "=r" (ret)); \
		} else { \
			if ((address%4) == 0) { \
				asm volatile ("/*XLMGet" #name "*/ lwz %[reg]," #address "(0)\n" : [reg] "=r" (ret)); \
			} else { \
				uint32_t hi, lo; \
				asm volatile ("/*XLMGet" #name "*/ lhz %[reg]," #address "(0)\n" : [reg] "=r" (hi)); \
				asm volatile ("/*XLMGet" #name "*/ lhz %[reg]," #address "+2(0)\n" : [reg] "=r" (lo)); \
				return (type)((hi << 16) | lo); \
			} \
		} \
		return ret; \
	} \
	static inline void XLMSet##name(type val) { \
		if (sizeof (type) == 1) { \
			asm volatile ("/*XLMSet" #name "*/ stb %[reg]," #address "(0)\n" :: [reg] "r" (val)); \
		} else if (sizeof (type) == 2) { \
			asm volatile ("/*XLMSet" #name "*/ sth %[reg]," #address "(0)\n" :: [reg] "r" (val)); \
		} else { \
			if ((address%4) == 0) { \
				asm volatile ("/*XLMSet" #name "*/ stw %[reg]," #address "(0)\n" :: [reg] "r" (val)); \
			} else { \
				asm volatile ("/*XLMSet" #name "*/ sth %[reg]," #address "(0)\n" :: [reg] "r" ((uint32_t)val >> 16)); \
				asm volatile ("/*XLMSet" #name "*/ sth %[reg]," #address "+2(0)\n" :: [reg] "r" (val)); \
			} \
		} \
	}

#else // 68K
#define MAKE_LM_ACCESSOR_STRING(address, type, name) \
	static inline const type XLMGet##name(void) { \
		const type ret; \
		asm volatile ("/*XLMGet" #name "*/ lea " #address ",%[reg]\n" : [reg] "=a" (ret)); \
		return ret; \
	}
#define MAKE_LM_ACCESSOR(address, type, name) \
	static inline type XLMGet##name(void) { \
		type ret; \
		if (sizeof (type) == 1) { \
			asm volatile ("/*XLMGet" #name "*/ move.b " #address ",%[reg]\n" : [reg] "=g" (ret)); \
		} else if (sizeof (type) == 2) { \
			asm volatile ("/*XLMGet" #name "*/ move.w " #address ",%[reg]\n" : [reg] "=g" (ret)); \
		} else { \
			asm volatile ("/*XLMGet" #name "*/ move.l " #address ",%[reg]\n" : [reg] "=g" (ret)); \
		} \
		return ret; \
	} \
	static inline void XLMSet##name(type val) { \
		if (sizeof (type) == 1) { \
			asm volatile ("/*XLMSet" #name "*/ move.b %[reg]," #address "\n" :: [reg] "g" (val)); \
		} else if (sizeof (type) == 2) { \
			asm volatile ("/*XLMSet" #name "*/ move.w %[reg]," #address "\n" :: [reg] "g" (val)); \
		} else { \
			asm volatile ("/*XLMSet" #name "*/ move.l %[reg]," #address "\n" :: [reg] "g" (val)); \
		} \
	}
#endif

MAKE_LM_ACCESSOR(0x2b6, char *, ExpandMem)
MAKE_LM_ACCESSOR(0x34e, char *, FCBSPtr) // TN1184 OS 9.0 makes this crash
MAKE_LM_ACCESSOR(0x372, char *, WDCBsPtr)
MAKE_LM_ACCESSOR(0x384, int16_t, DefVRefNum)
MAKE_LM_ACCESSOR(0x3f6, int16_t, FSFCBLen) // TN1184 OS 9.0 makes this suspect
MAKE_LM_ACCESSOR(0x8cf, uint8_t, CrsrCouple)
MAKE_LM_ACCESSOR(0x8ee, char *, JCrsrTask)
MAKE_LM_ACCESSOR(0xc20, int16_t, RowBits)
MAKE_LM_ACCESSOR(0xc22, int16_t, ColLines)
MAKE_LM_ACCESSOR_STRING(0x2e0, unsigned char *, FinderName);
MAKE_LM_ACCESSOR_STRING(0x910, unsigned char *, CurApName);
