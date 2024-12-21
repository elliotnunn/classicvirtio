/* Copyright (c) 2024 Elliot Nunn */
/* Licensed under the MIT license */

#include <stdbool.h>
#include <stdint.h>

#include <LowMem.h>

#include "extralowmem.h"
#include "printf.h"
#include "log.h"

bool LogEnable;
char LogPrefix[32];
static volatile char *reg;

// These all return the address of a hardware register that prints to the outside world
volatile char *virtioSerialRegister(void); // defined in log-*.c
static volatile char *sccSerialRegister(void); // not used, kept around in case something breaks

void InitLog(void) {
	if (0) { // backup log output that Mac OS dislikes
		reg = sccSerialRegister();
	} else {
		reg = virtioSerialRegister();
	}
	LogEnable = (reg!=NULL);
}

// Can't trust the alignment of this structure
struct expandMem {
	int16_t version;
	int16_t sizeHigh; // ignore this, will always be zero
	int16_t size;
	char padding[0x31e];
	unsigned char *progressString;
} __attribute__((packed));

// proto in printf.h, called by printf.c
// For speed there is no check here, but do not call if reg is null
void _putchar(char character) {
	static bool newline = true;
	if (newline) {
		// Get the OS 9.2 boot progress string if possible
		struct expandMem *em = (struct expandMem *)XLMGetExpandMem();

		if (em->size>=sizeof *em && em->progressString!=NULL) {
			*reg = '[';
			for (int i=0; i<em->progressString[0]; i++) {
				*reg = em->progressString[1+i];
			}
			*reg = ']';
		} else if ((XLMGetCurApName()[0]&0x80) == 0) {
			*reg = '[';
			for (int i=0; i<XLMGetCurApName()[0]; i++) {
				*reg = XLMGetCurApName()[1+i];
			}
			*reg = ']';
		}

		const char *p = LogPrefix;
		while (*p) *reg = *p++;
	}
	newline = (character=='\n');
	*reg = character;
}

static volatile char *sccSerialRegister(void) {
	volatile char *acontrol = LMGetSCCWr() + 2;
	volatile char *adata = LMGetSCCWr() + 6;

	*acontrol=9;  *acontrol=0x80; // Reinit serial.... reset A/modem
	*acontrol=4;  *acontrol=0x48; // SB1 | X16CLK
	*acontrol=12; *acontrol=0;    // basic baud rate
	*acontrol=13; *acontrol=0;    // basic baud rate
	*acontrol=14; *acontrol=3;    // baud rate generator = BRSRC | BRENAB
	*acontrol=5;  *acontrol=0xca; // enable tx, 8 bits/char, set RTS & DTR

	return adata;
}
