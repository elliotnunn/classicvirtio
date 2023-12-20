/* Copyright (c) 2023 Elliot Nunn */
/* Licensed under the MIT license */

// Header-only file for measuring ticks used by a function

// "Ticks" is a 32-bit counter at low-memory 0x16a of 60 Hz ticks

#define TIMESTART(counter) counter -= (((unsigned long)*(volatile unsigned short *)0x16a << 16) + *(volatile unsigned short *)0x16c)
#define TIMESTOP(counter) counter += (((unsigned long)*(volatile unsigned short *)0x16a << 16) + *(volatile unsigned short *)0x16c)

#define TIMEFUNC(counter) \
	TIMESTART(counter); \
	unsigned long *FNCCTR __attribute__ ((__cleanup__(timingCleanup))) = &(counter);

static void timingCleanup(unsigned long **counter) {
	TIMESTOP(**counter);
}
