/* Copyright (c) 2023 Elliot Nunn */
/* Licensed under the MIT license */

/*
Produce a Mac OS DRVR blob
Requires: drvr.s to be build, drvrOpen etc C functions, a drvrNameVers global
*/

ENTRY(drvrOpen);                   /* causes .drvr section to be kept */

SECTIONS {
	. = 0;
	.drvr : {              /* extract with `objcopy -j .drvr -O binary` */
		PROVIDE(relocationZero = .);

		/* create the DRVR header using a linker script */
		KEEP(*(*.drvrFlags));      /* 2 bytes */
		SHORT(0);                  /* drvrDelay (set in DCE, not here) */
		SHORT(0);                  /* drvrEMask, for desk accessories */
		SHORT(0);                  /* drvrMenu, for desk accessories */
		SHORT(drvrOpen);           /* code offsets ... */
		SHORT(drvrPrime);
		SHORT(drvrControl);
		SHORT(drvrStatus);
		SHORT(drvrClose);

		KEEP(*(*.drvrNameVers));   /* better be a pascal string */
		. = ALIGN(2);

		QUAD(0x544845434f44453a)   /* "THECODE:" eyecatcher */
		*(.text.drvr);             /* needs to be within 32k of start */
		*(.text .text.*);

		QUAD(0x474c4f42414c533a)   /* "GLOBALS:" eyecatcher */
		*(.sdata .sdata.* .data .data.* .rodata .rodata.* .bss .bss.* COMMON);

		/* is there a better way to make symbols mandatory? */
		must_have = drvrFlags + drvrNameVers;

		. = ALIGN(2);
		QUAD(0x52454c4f43415445)   /* "RELOCATE" eyecatcher */
		PROVIDE(relocationList = .);
	}

	/DISCARD/ : { *(*) }
}
