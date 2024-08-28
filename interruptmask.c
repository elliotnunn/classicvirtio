/* Copyright (c) 2024 Elliot Nunn */
/* Licensed under the MIT license */

/*
On both 68k and PowerPC, we change the 68k Status Register for two reasons:
- protect critical sections against reentrancy
- block Qemu pending an interrupt, to save CPU cycles

On the Classic Mac OS, all NuBus/PCI interrupts go through 68k interrupt vector #2.
Enabling and disabling this vector is done through the 68k SR register:
even PowerPC code needs to call through to emulated 68k code to access SR.

An alternative would be to use the Deferred Task Manager,
but this is unsuitable for block devices backing Virtual Memory.
*/

#include <Memory.h>
#include <MixedMode.h>
#include <Multiprocessing.h>
#include <Types.h>

#include "printf.h"

#include "interruptmask.h"

bool Interruptible(short sr) {
	return ((sr & 0x700) < 0x200);
}

short DisableInterrupts(void) {
	short oldlevel;

	#if GENERATINGCFM
		static const unsigned short code[] = {
			0x40c0,         // move.w sr,d0
			0x007c, 0x0700, // or.w   #$700,sr
			0x4e75          // rts
		};
		oldlevel = CallUniversalProc((void *)code, kCStackBased | RESULT_SIZE(SIZE_CODE(2)));
	#else
		__asm__ __volatile__ (
			" move.w %%sr,%[oldlevel];"
			" ori.w  #0x700,%%sr;"
			: [oldlevel] "=d" (oldlevel) // output
			: // input
			: // clobber
		);
	#endif

	return oldlevel;
}

void ReenableInterrupts(short oldlevel) {
	#if GENERATINGCFM
		static const unsigned short code[] = {
			0x46c0, // move.w d0,sr
			0x4e75  // rts
		};
		CallUniversalProc((void *)code, kRegisterBased | REGISTER_ROUTINE_PARAMETER(1, kRegisterD0, SIZE_CODE(2)), oldlevel);
	#else
		__asm__ __volatile__ (
			" move.w %[oldlevel],%%sr"
			: // output
			: [oldlevel] "d" (oldlevel)
			: "cc" // clobber
		);
	#endif
}

// Wait efficiently for an interrupt by sleeping the virtual CPU
void ReenableInterruptsAndWaitFor(short oldlevel, volatile unsigned long *flag) {
#if GENERATINGCFM
	// Unfortunately "MPDelayForSys" costs about 1 ms of overhead -- why?
	// so on PowerPC we still need to busyloop
	ReenableInterrupts(oldlevel);
	while (*flag == 0) {}
#else
	// Editable piece of machine code containing the STOP instruction:
	// STOP takes an "immediate operand" but we need to call it
	// with any SR value (various interrupt mask levels etc)
	static unsigned short code[6] = { // needs to be 12 bytes for BlockMove to clear it out
		0x7008,         // moveq #8,d0 // "EnterSupervisorMode"
		0xa08d,         // _DebugUtil
		0x4e72, 0x9999, // stop #placeholder
		0x4e75          // rts
	};

	// Edit the code only when the desired SR value is different (rare),
	// using the "BlockMove 12 bytes or more" trick to clear the i-cache.
	if ((code[3] & 0xff00) != (oldlevel & 0xff00)) {
		code[3] = oldlevel & 0xff00;
		BlockMove(code, code, 12);
	}

	// Skip EnterSupervisorMode (the first two instructions) if already in that mode
	unsigned short *jumpto;
	if ((oldlevel & 0x2000) == 0) { // check "S" bit of SR
		jumpto = code; // do EnterSupervisorMode
	} else {
		jumpto = code + 2; // don't EnterSupervisorMode
	}

	for (;;) {
		// Call STOP
		// (Use asm to work around a problem with 68k codegen:
		// compiler tries to make a PC-rel jump)
		__asm__ __volatile__ (
			" jsr (%[jumpto])"
			: // output
			: [jumpto] "a" (jumpto)
			: "cc", "d0", "d1", "d2", "a0", "a1" // standard function clobbers (by EnterSupervisorMode)
		);

		if (*flag != 0) {
			break;
		}

		// Woken up prematurely, so go back to sleep for another STOP
		DisableInterrupts();

		// Close a race condition by polling one more time
		if (*flag != 0) {
			ReenableInterrupts(oldlevel);
			break;
		}
	}
#endif
}
