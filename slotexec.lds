/* Copyright (c) 2023 Elliot Nunn */
/* Licensed under the MIT license */

/*
Produce a Slot Manager "sExecBlock"
Requires: void exec(struct SEBlock *pb); to be compiled with -mpcrel
Abhors: relocations (i.e. globals pointing to other globals)
*/

SECTIONS {
	. = 0;
	.exec : {              /* extract with `objcopy -j .exec -O binary` */
		PROVIDE(_start = .);

		/* Header */
		LONG(_end - .);
		BYTE(2); /* revision level always 2 */
		BYTE(1); /* 68000 */
		SHORT(0); /* reserved */
		LONG(4); /* offset to code */

		/* Thunk for C calling convention */
		SHORT(0x2f08);   /* move.l  a0,-(sp) */
		SHORT(0x6100);   /* bsr.s            */
		SHORT(exec - .); /*         exec     */
		SHORT(0x584f);   /* addq    #4,sp    */
		SHORT(0x4e75);   /* rts              */

		/* Code and data */
		*(.text .text.*);
		*(.sdata .sdata.* .data .data.* .rodata .rodata.* .bss .bss.* COMMON);

		. = ALIGN(2);

		PROVIDE(_end = .);
	}

	/DISCARD/ : { *(*) }
}
