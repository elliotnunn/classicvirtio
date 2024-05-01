/* Copyright (c) 2023 Elliot Nunn */
/* Licensed under the MIT license */

	/* random defines and macros that we would normally get from MPW */
	.macro OSLstEntry Id,Offset
	.long (\Id<<24)+((\Offset-.)&0xFFFFFF)
	.endm
	.macro DatLstEntry Id,Data
	.long (\Id<<24)+(\Data&0xFFFFFF)
	.endm
	.set sCPU_68020, 2
	.set endOfList, 255
	.set sRsrcType, 1
	.set sRsrcName, 2
	.set sRsrcIcon, 3
	.set sRsrcDrvrDir, 4
	.set sRsrcLoadRec, 5
	.set sRsrcBootRec, 6
	.set sRsrcFlags, 7
	.set sRsrcHWDevId, 8
	.set minorBase, 10
	.set minorLength, 11
	.set majorBase, 12
	.set majorLength, 13
	.set sRsrcCicn, 15
	.set boardId, 32
	.set primaryInit, 34
	.set vendorInfo, 36
	.set sGammaDir, 64
	.set vendorId, 1
	.set revLevel, 3
	.set partNum, 4
	.set mVidParams, 1
	.set mPageCnt, 3
	.set mDevType, 4
	.set oneBitMode, 128
	.set twoBitMode, 129
	.set fourBitMode, 130
	.set eightBitMode, 131
	.set sixteenBitMode, 132
	.set thirtyTwoBitMode, 133
	.set catBoard, 1
	.set catDisplay, 3
	.set typeBoard, 0
	.set typeVideo, 1
	.set typeVideo, 1
	.set typeDesk, 2
	.set drSwMacCPU, 0
	.set catCPU, 10
	/* SpBlock; parameter block for Slot Manager routines */
	.struct 0
spResult: .space 4
spsPointer: .space 4
spSize: .space 4
spOffsetData: .space 4
spIOFileName: .space 4
spsExecPBlk: .space 4
spParamData: .space 4
spMisc: .space 4
spReserved: .space 4
spIOReserved: .space 2
spRefNum: .space 2
spCategory: .space 2
spCType: .space 2
spDrvrSW: .space 2
spDrvrHW: .space 2
spTBMask: .space 1
spSlot: .space 1
spID: .space 1
spExtDev: .space 1
spHwDev: .space 1
spByteLanes: .space 1
spFlags: .space 1
spKey: .space 1
spBlock.size:
	/* SEBlock; parameter block for SExec blocks (like PrimaryInit) */
	.struct 0
seSlot: .space 1
sesRsrcId: .space 1
seStatus: .space 2
seFlags: .space 1
seFiller0: .space 1
seFiller1: .space 1
seFiller2: .space 1
seResult: .space 4
seIOFileName: .space 4
seDevice: .space 1
sePartition: .space 1
seOSType: .space 1
seReserved: .space 1
seRefNum: .space 1
seNumDevices: .space 1
seBootState: .space 1
filler: .space 1
SEBlock.size:
	.set JIODone, 0x08FC
	.set JVBLTask, 0x0D28
	.set ioTrap, 6
	.set ioResult, 16
	.set noQueueBit, 9
	.set dCtlSlot, 40


	.globl _start
	.section .text
_start:
ROMSTART:

/* The list of all the sResources on the ROM */
.set resNum, 128

sResourceDirectory:
	OSLstEntry 1, BoardResource

.rept 32 /* Maximum number of 9P devices */
	OSLstEntry resNum, Resource9P
.set resNum, resNum+1
.endr

.rept 1 /* Maximum number of input devices */
OSLstEntry resNum, ResourceInput
.set resNum, resNum+1
.endr

	DatLstEntry endOfList, 0

/* Information applying to the "physical" board */
BoardResource:
	OSLstEntry sRsrcType, BoardTypeRec
	OSLstEntry sRsrcName, BoardName
	OSLstEntry sRsrcIcon, Icon
	OSLstEntry sRsrcCicn, Cicn
	DatLstEntry boardId, 0x9545
	OSLstEntry primaryInit, PrimaryInitRec
	OSLstEntry vendorInfo, VendorInfoRec
	DatLstEntry endOfList, 0
BoardTypeRec:
	.short catBoard /* Category: Board! */
	.short typeBoard /* Type: Board! */
	.short 0 /* DrvrSw: 0, because it's a board */
	.short 0 /* DrvrHw: 0, because... it's a board */
BoardName:
	.asciz "Virtio bus"
	.align 2
PrimaryInitRec:
	.incbin "build/classic/slotexec-primaryinit"

VendorInfoRec:
	OSLstEntry vendorId, VendorId
	OSLstEntry revLevel, RevLevel
	OSLstEntry partNum, PartNum
	DatLstEntry endOfList, 0
VendorId:
	.asciz "QEMU"
	.align 2
RevLevel:
	.asciz "1.0"
	.align 2
PartNum:
	.asciz "VirtioBus"
	.align 2

/* This is SolraBizna's icon, perhaps should change */
Icon:  .long 0x000FF000,0x007FFE00,0x01FFFF80,0x03E3FFC0,0x07C01FE0,0x0FC00FF0
	.long 0x1FC1CFF8,0x3F81C7FC,0x3F0001FC,0x7F03C07E,0x7F003F3E,0x7E0000BE
	.long 0xFE003FFF,0xFE007FFF,0xFE01FFFF,0xFE01FFFF,0xFF81FFFF,0xFFC0FFFF
	.long 0xFFF0FFFF,0xFFF87FFF,0x7FFC7FFE,0x7FFE7FFE,0x7FFE3FFE,0x3FFF3FFC
	.long 0x3FFF3FFC,0x1FFFBFF8,0x0FFFBFF0,0x07FFFFF8,0x03FFFFF8,0x01FFFFFC
	.long 0x007FFE7C,0x000FF03E
	/* Now in color! */
Cicn:  .long CicnEnd-Cicn
	.long 0x00000000,0x80100000,0x00000020,0x00200000,0x00000000,0x00000048
	.long 0x00000048,0x00000000,0x00040001,0x00040000,0x00000000,0x00000000
	.long 0x00000000,0x00000004,0x00000000,0x00200020,0x00000000,0x00040000
	.long 0x00000020,0x00200000,0x0000000F,0xF000007F,0xFE0001FF,0xFF8003FF
	.long 0xFFC007FF,0xFFE00FFF,0xFFF01FFF,0xFFF83FFF,0xFFFC3FFF,0xFFFC7FFF
	.long 0xFFFE7FFF,0xFFFE7FFF,0xFFFEFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF
	.long 0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFF7FFF,0xFFFE7FFF
	.long 0xFFFE7FFF,0xFFFE3FFF,0xFFFC3FFF,0xFFFC1FFF,0xFFF80FFF,0xFFF007FF
	.long 0xFFF803FF,0xFFF801FF,0xFFFC007F,0xFE7C000F,0xF03EFFFF,0xFFFFFFFF
	.long 0xFFFFFFFF,0xFFFFFFE3,0xFFFFFFC0,0x1FFFFFC0,0x0FFFFFC1,0xCFFFFF81
	.long 0xC7FFFF00,0x01FFFF03,0xC07FFF00,0x3F3FFE00,0x00BFFE00,0x3FFFFE00
	.long 0x7FFFFE01,0xFFFFFE01,0xFFFFFF81,0xFFFFFFC0,0xFFFFFFF0,0xFFFFFFF8
	.long 0x7FFFFFFC,0x7FFFFFFE,0x7FFFFFFE,0x3FFFFFFF,0x3FFFFFFF,0x3FFFFFFF
	.long 0xBFFFFFFF,0xBFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF
	.long 0xFFFF0000,0x00000000,0x000A0000,0xFFFF6666,0x33330001,0xCCCCCCCC
	.long 0xCCCC0002,0xBBBBBBBB,0xBBBB0003,0xAAAAAAAA,0xAAAA0004,0x88888888
	.long 0x88880005,0x77777777,0x77770006,0x55555555,0x55550007,0x44444444
	.long 0x44440008,0x22222222,0x22220009,0x11111111,0x1111000F,0x00000000
	.long 0x0000FFFF,0xFFFFFFFF,0x11111111,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFF12
	.long 0x12121212,0x12FFFFFF,0xFFFFFFFF,0xFFFF2222,0x22222222,0x2222FFFF
	.long 0xFFFFFFFF,0xFFF23230,0x00323232,0x32323FFF,0xFFFFFFFF,0xFF333300
	.long 0x00000003,0x333333FF,0xFFFFFFFF,0xFF434300,0x00000000,0x434343FF
	.long 0xFFFFFFFF,0xF4444400,0x000FFF00,0x4444444F,0xFFFFFFFF,0xF4545000
	.long 0x000FFF00,0x0454545F,0xFFFFFFFF,0xF5550000,0x00000000,0x0005555F
	.long 0xFFFFFFFF,0xF5650000,0x00FFFF00,0x0000056F,0xFFFFFFFF,0xFF660000
	.long 0x000000FF,0xFFFF00FF,0xFFFFFFFF,0xFF700000,0x00000000,0x0000F0FF
	.long 0xFFFFFFFF,0xFFF00000,0x00000077,0x77777FFF,0xFFFFFFFF,0xFFF00000
	.long 0x00000787,0x8787FFFF,0xFFFFFFFF,0xFFF00000,0x00088888,0x88FFFFFF
	.long 0xFFFFFFFF,0xFFF00000,0x00089898,0xFFFFFFFF,0xFFFFFFFF,0xFFFFF000
	.long 0x000FFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFF00,0x0000FFFF,0xFFFFFFFF
	.long 0xFFFFFFFF,0xFFFFFFFF,0x0000FFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF
	.long 0xF0000FFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFF000FFF,0xFFFFFFFF
	.long 0xFFFFFFFF,0xFFFFFFFF,0xFFF00FFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF
	.long 0xFFF000FF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFF00FF,0xFFFFFFFF
	.long 0xFFFFFFFF,0xFFFFFFFF,0xFFFF00FF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF
	.long 0xFFFFF0FF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFF0FF,0xFFFFFFFF
	.long 0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF
	.long 0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF
	.long 0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF
	.long 0xFFFFFFFF,0xFFFFFFFF
	.byte 0xFF
	.align 2
CicnEnd:

Resource9P:
	OSLstEntry sRsrcType, 1$
	OSLstEntry sRsrcName, 2$
	OSLstEntry sRsrcLoadRec, SharedDriverLoader
	OSLstEntry sRsrcBootRec, BootRec
	DatLstEntry sRsrcFlags, 2 /* open at start, use 32-bit addressing */
	DatLstEntry sRsrcHWDevId, 1
	OSLstEntry 199, 3$
	DatLstEntry endOfList, 0
1$:
	.short catCPU
	.short typeDesk
	.short drSwMacCPU
	.short 0x5609
2$:
	.asciz "Virtio9P" /* without a leading dot */
	.align 2
3$:
	.incbin "build/classic/drvr-9p.elf"

ResourceInput:
	OSLstEntry sRsrcType, 1$
	OSLstEntry sRsrcName, 2$
	OSLstEntry sRsrcLoadRec, SharedDriverLoader
	DatLstEntry sRsrcFlags, 2 /* open at start, use 32-bit addressing */
	DatLstEntry sRsrcHWDevId, 1
	OSLstEntry 199, 3$
	DatLstEntry endOfList, 0
1$:
	.short catCPU
	.short typeDesk
	.short drSwMacCPU
	.short 0x5612
2$:
	.asciz "VirtioInput" /* without a leading dot */
	.align 2
3$:
	.incbin "build/classic/drvr-input.elf"

/* Work around the 64K driver limitation in the Slot Manager */
SharedDriverLoader:
	.incbin "build/classic/slotexec-drvrload"

BootRec:
	.incbin "build/classic/slotexec-boot"

/* Pad so that, after the header, this ROM is a multiple of 4K */
/* Works around a suspected QEMU bug: */
/* Assertion failed: (!(iotlb & ~TARGET_PAGE_MASK)), function tlb_set_page_full */
	.org ((.+0xfff+0x14)&~0xfff)-0x14

/* Header, with magic number, must go at the end */
	.long (sResourceDirectory-.) & 0xffffff
	.long ROMEND-ROMSTART
	.long 0 /* Checksum goes here */
	.byte 1 /* ROM format revision */
	.byte 1 /* Apple format */
	.long 0x5A932BC7 /* Magic number */
	.byte 0 /* Must be zero */
	.byte 0x0F /* use all four byte lanes */
ROMEND:
	.end
