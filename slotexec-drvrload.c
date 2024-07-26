/* Copyright (c) 2023 Elliot Nunn */
/* Licensed under the MIT license */

// Load DRVRs over 64K because the Slot Manager can't

#include <Devices.h>
#include <LowMem.h>
#include <Memory.h>
#include <Slots.h>
#include <ROMDefs.h>

#include <stdint.h>
#include <string.h>

struct elf {
	char e_ident[16];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint32_t e_entry;
	uint32_t e_phoff;
	uint32_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
};

struct phdr {
	uint32_t p_type;
	uint32_t p_offset;
	uint32_t p_vaddr;
	uint32_t p_paddr;
	uint32_t p_filesz;
	uint32_t p_memsz;
	uint32_t p_flags;
	uint32_t p_align;
};

struct shdr {
	uint32_t sh_name;
	uint32_t sh_type;
	uint32_t sh_flags;
	uint32_t sh_addr;
	uint32_t sh_offset;
	uint32_t sh_size;
	uint32_t sh_link;
	uint32_t sh_info;
	uint32_t sh_addralign;
	uint32_t sh_entsize;
};

struct drvr {
	uint16_t flags;
	uint16_t delay;
	uint16_t emask;
	uint16_t menu;
	uint16_t open;
	uint16_t prime;
	uint16_t ctl;
	uint16_t status;
	uint16_t close;
	unsigned char name[32];
	uint16_t code[6]; // must be at least 12 bytes for BlockMove to clear i-cache
	char data[];
};

// Follow a path of struct numbers, returning a pointer or NULL
static const void *slotStruct(int slot, int srsrc, ...) {
	int err;

	struct SpBlock sp = {.spSlot=slot, .spID=srsrc};
	err = SRsrcInfo(&sp);
	if (err) return NULL;

	va_list args;
	va_start(args, srsrc);

	for (;;) {
		int id = va_arg(args, int);
		if (id == 0) break;

		sp.spID = id;
		err = SFindStruct(&sp);
		if (err) return NULL;
	}

	va_end(args);
	return sp.spsPointer;
}

void exec(struct SEBlock *pb) {
	int err;

	// see the workaround in slotexec-boot.c
	// VERY IMPORTANT
	if (LMGetToolScratch()[0] == 'V' && LMGetToolScratch()[1] == 'I') {
		pb->seSlot = LMGetToolScratch()[2];
		pb->sesRsrcId = LMGetToolScratch()[3];
	}

	char slot = pb->seSlot, srsrc = pb->sesRsrcId;

	const struct elf *elf = slotStruct(slot, srsrc, 199, 0);
	if (!elf) SysError(0x0e1f);

	// Macros for iterating over segments (the Program Header)
#define PH0 ((struct phdr *)((char *)elf + elf->e_phoff))
#define PHNEXT(h) ((struct phdr *)((char *)(h) + elf->e_phentsize))
#define PHEND ((struct phdr *)((char *)elf + elf->e_phoff + elf->e_phentsize*elf->e_phnum))

	// Macros for iterating over sections (the Section Header)
#define SH0 ((struct shdr *)((char *)elf + elf->e_shoff))
#define SHNEXT(h) ((struct shdr *)((char *)(h) + elf->e_shentsize))
#define SHEND ((struct shdr *)((char *)elf + elf->e_shoff + elf->e_shentsize*elf->e_shnum))

	void *newtext = NULL, *newdata = NULL;
	struct phdr *textseg = NULL, *dataseg = NULL;

	for (struct phdr *seg=PH0; seg<PHEND; seg=PHNEXT(seg)) {
		if (seg->p_type != 1) continue; // need LOAD

		if (seg->p_flags & 2) { // writable (probably the data segment)
			dataseg = seg;
		} else { // read-only (probably the code segment)
			textseg = seg;
			newtext = (char *)elf + seg->p_offset;
		}
	}

	if (!textseg || !dataseg) SysError(0x5555);

	// DRVR handle block = header + tiny 68k stub + data segment
	Handle hdl = NewHandleSysClear(sizeof (struct drvr) + dataseg->p_memsz);
	if (!hdl) SysError(0xdd00);
	HLock(hdl);
	struct drvr *drvr = (struct drvr *)*hdl;

	drvr->flags = dNeedLockMask|dStatEnableMask|dCtlEnableMask|dWritEnableMask|dReadEnableMask;
	drvr->open = drvr->prime = drvr->ctl = drvr->status = drvr->close = offsetof(struct drvr, code);

	const char *name = slotStruct(slot, srsrc, sRsrcName, 0);

	// Set the name field to .VirtioInput etc, leaving the version field as 00.0.0
	drvr->name[0] = 1 + strlen(name);
	drvr->name[1] = '.';
	strcpy(drvr->name+2, name);

	newdata = drvr->data;
	BlockMoveData((char *)elf + dataseg->p_offset, newdata, dataseg->p_filesz);

	void *entry = newtext + elf->e_entry - textseg->p_vaddr;

	// The tiniest possible 68k code stub to hand control to the ROM driver
	drvr->code[0] = 0x2f0d; // move.l  a5,-(sp)
	drvr->code[1] = 0x4bfa; // lea     drvr->data,a5
	drvr->code[2] = 0x0008;
	drvr->code[3] = 0x4ef9; // jmp     entry
	drvr->code[4] = (uint32_t)entry >> 16;
	drvr->code[5] = (uint32_t)entry;

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

	// Clear the icache for the whole DRVR block
	BlockMove(drvr, drvr, sizeof (struct drvr) + dataseg->p_filesz);

	pb->seResult = (long)hdl;
	pb->seFlags = 0;
	pb->seStatus = 0; // not seSuccess because the ROM inverts the meaning??
}
