/* Copyright (c) 2024 Elliot Nunn */
/* Licensed under the MIT license */

/* For toolchains lacking the raw_inline/-msep-data fix (May 2024) */
/* https://github.com/autc04/Retro68/issues/238 */

	.global BLOCKMOVEDATA
	.section .text.BLOCKMOVEDATA
BLOCKMOVEDATA:
	.short 0xa22e, 0x4e75

	.global DEBUGSTR
	.section .text.DEBUGSTR
DEBUGSTR:
	.short 0xabff, 0x4e75

	.global DISPOSEPTR
	.section .text.DISPOSEPTR
DISPOSEPTR:
	.short 0xa01f, 0x4e75

	.global NMINSTALL
	.section .text.NMINSTALL
NMINSTALL:
	.short 0xa05e, 0x4e75

	.global NMREMOVE
	.section .text.NMREMOVE
NMREMOVE:
	.short 0xa05f, 0x4e75

	.global PBOPENWDSYNC
	.section .text.PBOPENWDSYNC
PBOPENWDSYNC:
	.short 0xa260, 0x4e75

	.global PBSETVOLSYNC
	.section .text.PBSETVOLSYNC
PBSETVOLSYNC:
	.short 0xa015, 0x4e75

	.global READLOCATION
	.section .text.READLOCATION
READLOCATION:
	.short 0x203c, 0x000c, 0x00e4, 0xa051, 0x4e75

	.global SYSERROR
	.section .text.SYSERROR
SYSERROR:
	.short 0xa9c9, 0x4e75

	.global SynchronizeIO
	.section .text.SynchronizeIO
SynchronizeIO:
	.short 0x4e71, 0x4e75



	.global TICKCOUNT
	.section .text.TICKCOUNT
TICKCOUNT:
	.short 0xad75

	.global GETRESOURCE
	.section .text.GETRESOURCE
GETRESOURCE:
	.short 0xada0

	.global INITRESOURCES
	.section .text.INITRESOURCES
INITRESOURCES:
	.short 0xad95



	.global IntsOff
	.section .text.IntsOff
IntsOff:
	.short 0x40c0, 0x007c, 0x0700, 0x4e75

	.global IntsOn
	.section .text.IntsOn
IntsOn:
	.short 0x46c0, 0x4e75
