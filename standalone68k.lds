/* Copyright (c) Elliot Nunn */
/* Licensed under the MIT license */

/*
Produce standalone code suitable for running from ROM
Code be compiled with -mpcrel
No writable global variables allowed
*/

SECTIONS {
	. = 0;
	.text : {              /* extract with `objcopy -j .text -O binary` */
		/* Code and data */
		*(.text .text.*);
		*(.sdata .sdata.* .data .data.* .rodata .rodata.* .bss .bss.* COMMON);
		. = ALIGN(2);
	}
}
