/* Copyright (c) 2024 Elliot Nunn */
/* Licensed under the MIT license */

/*
Only for the 9P filesystem driver,
because it needs profiling (being complex and slow),
and has an easy output channel (a file).
*/

#include <Patches.h>
#include <Retrace.h>
#include <stdint.h>
#include <string.h>

#include "9p.h"
#include "callin68k.h"
#include "printf.h"

#include "profile.h"
#include "panic.h"

void __cyg_profile_func_enter(void *this_fn, void *call_site);
void __cyg_profile_func_exit(void *this_fn, void *call_site);
static void sample(void);
static void makeFuncTable(void);
static void addFunc(const void *address, const char *name);
static const char *getFuncName(const void *address);

static volatile void *shadowstack[400];
static volatile int depth;
static uint32_t outfid;
static uint64_t outseek;
static struct VBLTask timer = {
	.qType = vType,
	.vblAddr = CALLIN68K_C_ARG0_GLOBDEF(sample),
	.vblCount = 1,
};

void InitProfile(uint32_t fid) {
	outfid = fid;
	char header[] = // can't be const because Write9 needs actual RAM :(
		"#!/bin/sh\n"
		"# run me and pipe me into flamegraph.pl\n"
		"exec awk 'NR>3 {count[$1]++} END {for (word in count) print word, count[word]}' \"$0\"\n";
	Write9(outfid, header, 0, sizeof header - 1, NULL);
	outseek = sizeof header - 1;
	makeFuncTable();
	VInstall((void *) &timer);
}

// thanks to -finstrument-functions
__attribute__((no_instrument_function))
void __cyg_profile_func_enter(void *this_fn, void *call_site) {
	shadowstack[depth++] = this_fn;
}

__attribute__((no_instrument_function))
void __cyg_profile_func_exit(void *this_fn, void *call_site) {
	depth--;
}

__attribute__((no_instrument_function))
static void sample(void) {
	timer.vblCount = 1; // call it again

	char line[200];
	char *p = line;

	if (depth == 0) {
		p = stpcpy(p, "nothing");
	} else {
		for (int i=0; i<depth; i++) {
			p = stpcpy(p, getFuncName((const void *)shadowstack[i]));
			*p++ = ';';
		}
		p--; // cut the last semicolon
	}
	*p++ = '\n';

	Write9(outfid, line, outseek, p-line, NULL);
	outseek += p-line;
}

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
	char rest[];
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

struct sym {
	uint32_t st_name;  // Symbol name (index into string table)
	uint32_t st_value; // Value or address associated with the symbol
	uint32_t st_size;  // Size of the symbol
	uint8_t st_info;   // Symbol's type and binding attributes
	uint8_t st_other;  // Must be zero; reserved
	uint16_t st_shndx; // Which section (header table index) it's defined in
};

static void makeFuncTable(void) {
	extern const struct elf __executable_start[]; // thanks linker script
	const struct elf *elf = __executable_start; // decay to a pointer

	// Macros for iterating over segments (the Program Header) and sections (the Section Header)
	#define PH0 ((struct phdr *)((char *)elf + elf->e_phoff))
	#define PHNEXT(h) ((struct phdr *)((char *)(h) + elf->e_phentsize))
	#define PHEND ((struct phdr *)((char *)elf + elf->e_phoff + elf->e_phentsize*elf->e_phnum))
	#define SH0 ((struct shdr *)((char *)elf + elf->e_shoff))
	#define SHNEXT(h) ((struct shdr *)((char *)(h) + elf->e_shentsize))
	#define SHEND ((struct shdr *)((char *)elf + elf->e_shoff + elf->e_shentsize*elf->e_shnum))

	// need .shstrtab
	const struct shdr *shstrtab = (const void *)elf + elf->e_shoff + (long)elf->e_shstrndx*elf->e_shentsize;
	const char *shstrs = (const void *)elf + shstrtab->sh_offset;

	// need .symtab (a 16-byte struct per function) and .strtab (their names)
	const struct shdr *symtab = NULL, *strtab = NULL;
	for (const struct shdr *sec=SH0; sec<SHEND; sec=SHNEXT(sec)) {
		if (sec->sh_type == 2 /*SHT_SYMTAB*/) {
			symtab = sec;
		} else if (!strcmp(shstrs+sec->sh_name, ".strtab")) {
			strtab = sec;
		}
	}
	if (symtab==NULL || strtab==NULL) panic("missing string table");
	const char *symstrs = (const void *)elf + strtab->sh_offset;

	// For each symbol, find its real address via the program table, and addFunc it
	for (int i=0; i<symtab->sh_size; i+=16) {
		const struct sym *sym = (void *)elf + symtab->sh_offset + i;
		if ((sym->st_info&0xf) != 2 /*STT_FUNC*/) {
			continue;
		}
		for (const struct phdr *seg=PH0; seg<PHEND; seg=PHNEXT(seg)) {
			if (sym->st_value>=seg->p_vaddr && sym->st_value<seg->p_vaddr+seg->p_memsz) {
				char *addr = (char *)elf + sym->st_value - seg->p_vaddr + seg->p_offset;
				addFunc(addr, symstrs+sym->st_name);
				break;
			}
		}
	}
}

struct f {
	const void *address;
	const char *name;
};

static struct f functab[512];

static void addFunc(const void *address, const char *name) {
	uint16_t hash = (uint32_t)address * 5 / 2; // assume functions smoothly distributed at even addresses
	while (functab[hash % (sizeof functab/sizeof *functab)].address) {
		hash++; // linear probing
	}
	functab[hash % (sizeof functab/sizeof *functab)] = (struct f){address, name};
}

static const char *getFuncName(const void *address) {
	uint16_t hash = (uint32_t)address * 5 / 2;
	for (;;) {
		if (functab[hash % (sizeof functab/sizeof *functab)].address == NULL) {
			return "?";
		} else if (functab[hash % (sizeof functab/sizeof *functab)].address == address) {
			return functab[hash % (sizeof functab/sizeof *functab)].name;
		}
		hash++;
	}
}
