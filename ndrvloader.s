/* Copyright (c) 2023 Elliot Nunn */
/* Licensed under the MIT license */

/*
This assembly code is the entry point from Open Firmware to C code.

Tricky calling convention stuff:

The IBM ABI uses r2 as a global base register, and all "far" function calls
are through a transition vector:
	struct tvector {void *code; void *r2value;};

Our XCOFF entry point is actually such a tvector. But Open Firmware only half-
understands this structure: it jumps to the code correctly, but doesn't set r2.

So we make a fake tvector, knowing that OF will ignore the second word:
*/

	.global entrytvec
	.section .data
entrytvec:
	.long r2setup
	.long 0

/*
This machine code cannot go in the .text section because it contains a relocation.
*/
	.section .data
r2setup:

/*
Get the address of the real tvector. (Use blrl because I can't code a lis/ori idiom?)
*/
	mflr    %r0
	bl      jumphere
	mflr    %r2
	lwz     %r2,0(%r2)
	mtlr    %r0

/*
Now follow this tvector so that the C code can find its globals.
*/
	lwz     %r0,0(%r2)
	mtctr   %r0
	lwz     %r2,4(%r2)
	bctr

jumphere:
	blrl
	.global ofmain
	.long   ofmain
