/* Copyright (c) 2023-2024 Elliot Nunn */
/* Licensed under the MIT license */

/*
Just stub driver routines, no async support
The linker script implements the DRVR header
The DoDriverIO routine (in C) does the work
*/

	.global drvrROMEntry
	.global IOCommandIsComplete

	.section .text.drvrROMEntry
drvrROMEntry:

/* DRVR stub pushed the old A5, and set A5 to the data ptr, and JMPed here */

	movem.l %d2/%a0/%a1,-(%sp)      /* save more registers */

/* Push a DriverInitInfo/DriverFinalInfo struct, but don't fill it in */
	sub.w   #18,%sp

	moveq.l #0,%d0
	move.b  7(%a0),%d0              /* ioTrap number */

/* Push arguments */
	pea     4                       /* IOCommandKind = immed */
	move.l  %d0,-(%sp)              /* IOCommandCode */
	move.l  %a0,-(%sp)              /* IOCommandContents */
	clr.l   -(%sp)                  /* IOCommandID */
	move.l  #-1,-(%sp)              /* AddressSpaceID */

/* Special case for Open/Close calls: */
/* Convert to Initialize/Finalize, emulating the NDRV runtime */
	cmp.b   #1,%d0
	bhi.s   notOpenOrClose

	lea     20(%sp),%a0             /* replace PB with init/final info */
	move.l  %a0,8(%sp)

	move.w  24(%a1),(%a0)+ /* DCE.refnum */
	bsr     whichDeviceInSlot
	movem.l %d0/%d1,(%a0) /* RegEntryID becomes true sub-device address */

	addq.l  #7,12(%sp)              /* convert IOCommandCode */
notOpenOrClose:

/* DO IT, then clean up registers */
	bsr.l   DoDriverIO
	add.w   #20+18,%sp
	movem.l (%sp)+,%d2/%a0/%a1/%a5  /* we saved some, the DRVR stub saved A5 */

/* Peculiarly Open touches ioResult */
	move.w  6(%a0),%d1              /* get PB.ioTrap */
	tst.b   %d1
	bne.s   notOpenDontTouchResult
	move.w  %d0,16(%a0)
notOpenDontTouchResult:

/* Check noQueueBit ("immed" call?) */
	btst    #9,%d1
	bne.s   returnDirectly
	move.l  0x8fc,-(%sp)            /* jIODone */
returnDirectly:
	rts
	.byte   0x8c
	.ascii  "drvrROMEntry"
	.align  2

whichDeviceInSlot: /* expect DCE ptr in a1, return slot and index in d0/d1 */
	movem.l %d2-%d4/%a0-%a2,-(%sp)

	move.b  40(%a1),%d3 /* slot number */
	move.b  41(%a1),%d4 /* sResource number */

	moveq   #56/4-1,%d0 /* make an SpBlock */
1$:
	clr.l   -(%sp)
	dbf     %d0,1$
	move.l  %sp,%a0

	move.b  %d3,49(%a0) /* slot number */
	move.b  %d4,50(%a0) /* sResource number */
	moveq   #11,%d0 /* call SGetSRsrc */
	.short  0xa06e

	moveq.l #0,%d0
	move.b  %d3,%d0

	moveq.l #0,%d1 /* ioReserved field */
	move.w  36(%a0),%d1

	lea     56(%sp),%sp

	movem.l (%sp)+,%d2-%d4/%a0-%a2
	rts


	.section .text.IOCommandIsComplete
IOCommandIsComplete:
	moveq.l #0,%d0
	move.w  8(%sp),%d0
	rts
