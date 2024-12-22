/* Copyright (c) Elliot Nunn */
/* Licensed under the MIT license */

// Accept control from a DRVR stub (see StubDriver in declrom.s)
// Interpret the header of the ELF that we are linked inside, set up a runtime environment,
// and call through to device-*.c

#include <string.h>

#include <Devices.h>
#include <Errors.h>
#include <Memory.h>
#include <Slots.h>
#include <Types.h>

#include "cleanup.h"
#include "structs-elf.h"

#include "runtime.h"

static struct elf *elfHeader(void);
static void patchStubDriver(DRVRHeader *drvr);

// This is the ELF entry point so that the linker can do dead code elimination,
// but actually the Stub Driver finds us by scanning ROM for the Eyecatcher.
// It is the minimum assembly needed to get through to C. IM says we don't need to save registers.
asm (
	// OPEN (JMP'd to by the Stub Driver)
	".globl asmOpen                     \n"
	"asmOpen:                           \n"
	".short  0x6004, 0x6f70, 0x656e     \n" // eyecatcher
	"movem.l %a0/%a1,-(%sp)             \n" // args for the second C routine
	"move.l  %a1,-(%sp)                 \n" // DCE argument
	"bsr.l   ramDataSegment             \n" // call first low-level C routine (can't access A5 globals)
	"addq    #4,%sp                     \n"
	"move.l  %d0,%a5                    \n" // this is now our global pointer
	"bsr.l   cOpen                      \n" // call second low-level C routine (can access A5 globals)
	"addq    #8,%sp                     \n"
	"ourSharedRTS:                      \n"
	"rts                                \n"

	// CLOSE (JMP'd to by the Stub Driver)
	".globl  asmClose                   \n"
	"asmClose:                          \n"
	"move.l  20(%a1),%a5                \n" // storage handle
	"move.l  (%a5),%a5                  \n" // storage pointer
	"move.l  %a1,-(%sp)                 \n" // argument
	"bsr.l   cClose                     \n" // low-level C routine (i.e. in this file)
	"addq    #4,%sp                     \n"
	"rts                                \n"

	// PRIME (JMP'd to by the Stub Driver)
	".globl asmPrime                    \n"
	"asmPrime:                          \n"
	"move.l  20(%a1),%a5                \n" // storage handle
	"move.l  (%a5),%a5                  \n" // storage pointer
	"move.w  6(%a0),%d3                 \n" // save trap number (survives C call)
	"move.l  %a1,%a2                    \n" // save DCE address (survives C call)
	"move.l  %a0,-(%sp)                 \n" // PB argument

	"cmp.b   #3,%d3                     \n"
	"bne.s   isRead                     \n"
	"pea     primeCommon                \n"
	"bra.l   DriverWrite                \n" // high-level C routine (i.e. not this file)
	"isRead:                            \n"
	"bsr.l   DriverRead                 \n" // high-level C routine (i.e. not this file)
	"primeCommon:                       \n"

	"commonCleanup:                     \n"
	"addq    #4,%sp                     \n" // pop argument
	"move.l  %a2,%a1                    \n" // restore DCE (needed for IODone)
	"btst    #9,%d3                     \n" // test immed bit in trap number
	"bne.s   ourSharedRTS               \n"
	"tst.w   %d0                        \n"
	"bgt.s   ourSharedRTS               \n"
	"jmp     ([0x8fc])                  \n" // jIODone

	// CONTROL (JMP'd to by the Stub Driver)
	".globl asmCtl                      \n"
	"asmCtl:                            \n"
	"move.l  20(%a1),%a5                \n" // storage handle
	"move.l  (%a5),%a5                  \n" // storage pointer
	"move.w  6(%a0),%d3                 \n" // save trap number (survives C call)
	"move.l  %a1,%a2                    \n" // save DCE address (survives C call)
	"move.l  %a0,-(%sp)                 \n" // PB argument
	"pea     commonCleanup              \n"
	"bra.l   DriverCtl                  \n" // high-level C routine (i.e. not this file)

	// STATUS (JMP'd to by the Stub Driver)
	".globl asmStatus                   \n"
	"asmStatus:                         \n"
	"move.l  20(%a1),%a5                \n" // storage handle
	"move.l  (%a5),%a5                  \n" // storage pointer
	"move.w  6(%a0),%d3                 \n" // save trap number (survives C call)
	"move.l  %a1,%a2                    \n" // save DCE address (survives C call)
	"move.l  %a0,-(%sp)                 \n" // PB argument
	"pea     commonCleanup              \n"
	"bra.l   DriverStatus               \n" // high-level C routine (i.e. not this file)
);

// First C function to be called at Driver Open
// Creates the global data area by reading the ELF header -- so it cannot access globals yet!
__attribute__((no_instrument_function))
void *ramDataSegment(AuxDCE *dce) {
	struct elf *elf = elfHeader();
	#define PH0 ((struct phdr *)((char *)elf + elf->e_phoff))
	#define PHNEXT(h) ((struct phdr *)((char *)(h) + elf->e_phentsize))
	#define PHEND ((struct phdr *)((char *)elf + elf->e_phoff + elf->e_phentsize*elf->e_phnum))
	#define SH0 ((struct shdr *)((char *)elf + elf->e_shoff))
	#define SHNEXT(h) ((struct shdr *)((char *)(h) + elf->e_shentsize))
	#define SHEND ((struct shdr *)((char *)elf + elf->e_shoff + elf->e_shentsize*elf->e_shnum))

	void *newtext = NULL, *newdata = NULL;
	const struct phdr *textseg = NULL, *dataseg = NULL;

	for (struct phdr *seg=PH0; seg<PHEND; seg=PHNEXT(seg)) {
		if (seg->p_type != 1) continue; // need LOAD

		if (seg->p_flags & 2) { // writable (probably the data segment)
			dataseg = seg;
		} else { // read-only (probably the code segment)
			textseg = seg;
			newtext = (char *)elf + seg->p_offset;
		}
	}

	if (!textseg) SysError(0x5555);
	if (!dataseg) SysError(0x6665); // might be okay not to have a data segment?

	// Copy data segment from ROM to RAM
	dce->dCtlStorage = NewHandleSysClear(dataseg->p_memsz);
	if (dce->dCtlStorage == NULL) SysError(0x0707);
	HLock(dce->dCtlStorage);
	newdata = *dce->dCtlStorage;
	BlockMoveData((char *)elf + dataseg->p_offset, newdata, dataseg->p_filesz);

	// Hand-edit suspected pointers in the data segment (it has come to this)
	for (long i=0; i<dataseg->p_filesz-4; i+=2) {
		uint32_t *slot = (uint32_t *)(newdata + i);

		if (*slot - textseg->p_vaddr < textseg->p_memsz) {
			*slot += (uint32_t)newtext - textseg->p_vaddr;
			i+=2;
		} else if (*slot - dataseg->p_vaddr < dataseg->p_memsz) {
			*slot += (uint32_t)newdata - dataseg->p_vaddr;
			i+=2;
		}
	}

	BlockMove(newdata, newdata, dataseg->p_memsz); // clear instruction cache
	return newdata;
}

// Second C function to be called at Driver Open, with global variables now available
// Do all the other stuff, and remember to set a result code in the Param Block
__attribute__((no_instrument_function))
void cOpen(FileParam *pb, AuxDCE *dce) {
	DRVRHeader *drvr = *(DRVRHeader **)dce->dCtlDriver; // trust me, it's a handle
	patchStubDriver(drvr);

	// Setting the ioResult field is an idiosyncrasy of the Open call
	pb->ioResult = DriverStart(dce->dCtlRefNum);
	if (pb->ioResult != noErr) {
		Cleanup();
	}
}

__attribute__((no_instrument_function))
int cClose(AuxDCE *dce) {
	int err = DriverStop();
	if (err == noErr) {
		Cleanup();
		DisposeHandle(dce->dCtlStorage); // delete global variable storage, must be the last thing
		dce->dCtlStorage = NULL;
	}
	return err;
}

// Scan backward in memory to find our own header: undefined behaviour in C so use asm.
__attribute__((no_instrument_function))
static struct elf *elfHeader(void) {
	struct elf *ret;
	asm volatile (
		"lea .,%[ret]\n"
		"1:\n"
		"subq #2,%[ret]\n"
		"cmp.l #0x7f454c46,(%[ret])\n"
		"bne.s 1b\n"
		: [ret] "=a" (ret)
	);
	return ret;
}

// These prototypes let patchStubDriver find the assembly routines
void asmPrime(void);
void asmCtl(void);
void asmStatus(void);
void asmClose(void);
__attribute__((no_instrument_function))
static void patchStubDriver(DRVRHeader *drvr) {
	// Hand-edit the DRVR header to jump to our four driver routines
	int fields[] = {drvr->drvrPrime, drvr->drvrCtl, drvr->drvrStatus, drvr->drvrClose};
	void *targets[] = {&asmPrime, &asmCtl, &asmStatus, &asmClose};
	for (int i=0; i<4; i++) {
		memcpy((char *)drvr + fields[i], "\x4e\xf9", 2);
		memcpy((char *)drvr + fields[i] + 2, &targets[i], 4);
		BlockMove((char *)drvr + fields[i], (char *)drvr + fields[i], 12); // clear i-cache
	}
}
