/* Copyright (c) 2023 Elliot Nunn */
/* Licensed under the MIT license */

/*
Just stub driver routines, no async support
The linker script implements the DRVR header
The DoDriverIO routine (in C) does the work
*/

	.global drvrOpen, drvrClose, drvrControl, drvrStatus, drvrPrime, dce
	.global IOCommandIsComplete
	.global drvrStart, relocationList

	.section .text.drvr
drvrOpen: drvrClose: drvrControl: drvrStatus: drvrPrime:

	movem.l %d2/%a0/%a1,-(%sp)      /* save registers */

/* Push a DriverInitInfo/DriverFinalInfo struct */
	sub.w   #18,%sp

	moveq.l #0,%d0
	move.b  7(%a0),%d0              /* ioTrap number */

	pea     4                       /* IOCommandKind = immed */
	move.l  %d0,-(%sp)              /* IOCommandCode */
	move.l  %a0,-(%sp)              /* IOCommandContents */
	clr.l   -(%sp)                  /* IOCommandID */
	move.l  #-1,-(%sp)              /* AddressSpaceID */

/* Special case for Open and Close calls */
	cmp.b   #1,%d0
	bhi.s   notOpenOrClose

	bsr     fixUpPointers

	lea     20(%sp),%a0             /* replace PB with init/final info */
	move.l  %a0,8(%sp)

	move.w  24(%a1),(%a0)+ /* DCE.refnum */
	bsr     whichDeviceInSlot
	movem.l %d0/%d1,(%a0) /* RegEntryID becomes true sub-device address */

	addq.l  #7,12(%sp)              /* convert IOCommandCode */
notOpenOrClose:

/* Opportunistically set DCE global */
	lea     dce(%pc),%a0
	move.l  %a1,(%a0)

	jsr     DoDriverIO
	add.w   #20+18,%sp

	movem.l (%sp)+,%d2/%a0/%a1

	move.w  6(%a0),%d1              /* get PB.ioTrap */

/* Peculiarly Open touches ioResult */
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
	.byte   0x84
	.ascii  "DRVR"
	.align  2


/* The post-link shell script creates a list of "interior pointers" */
/* Iterate the list and turn those pointers into real addresses */
/* Idempotent: replaces self with a simple rts after being run */
/* Important to use pc-relative addressing or we have a bootstrap problem */
fixUpPointers:
	movem.l %d0-%d1/%a0-%a2,-(%sp)
	lea     relocationZero(%pc),%a0 /* a0 = base register */
	move.l  %a0,%d0                 /* d0 = base register too */
	lea.l   relocationList,%a1      /* a1 = relocation list */
	add.l   %d0,%a1                 /* hack: can't use 16-bit pc-relative lea */

1$:
	move.l  (%a1)+,%d1              /* d1 = relocation offset */
	beq.s   2$
	lea     (%a0,%d1.l),%a2         /* a2 = address of ptr */
	add.l   %d0,(%a2)
	bra.s   1$
2$:

	lea     fixUpPointers(%pc),%a2  /* Self-modify: never run this func again */
	move.w  #0x4e75,(%a2)

	move.l  %a1,%d0                 /* To clear the 68k code cache... */
	sub.l   %a0,%d0
	move.l  %a0,%a1
	.short  0xa02e                  /* BlockMove-in-place! */

	movem.l (%sp)+,%d0-%d1/%a0-%a2
3$:
	rts


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


	.section .bss
dce:
	.long   0                       /* extern struct DCtlEntry *dce; */
