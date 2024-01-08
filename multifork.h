/* Copyright (c) 2023 Elliot Nunn */
/* Licensed under the MIT license */

#pragma once

// The formats
// multifork-1: Direct access to resource forks and Finder info on macOS hosts
// multifork-2: AppleDouble (._FILENAME)
// multifork-3: Elliot's favourite format (FILENAME.rdump FILENAME.idump)

// one challenge is to make an opaque struct available to all MF implementations

#include <stdbool.h>
#include <stdint.h>

// Field select
enum {
	MF_DSIZE = 1,
	MF_RSIZE = 2,
	MF_TIME  = 4,
	MF_FINFO = 8,
};

// At this point in the stack, the interface is a compromise
// between the 9P/Unix and Mac OS views of filesystem metadata.
// Note also that file size can only be changed on open files, via SetEOF.
struct MFAttr {
	uint64_t dsize, rsize;
	int64_t unixtime; // *SIGNED* -- let the upper layer translate to/from Mac time
	char finfo[16], fxinfo[16]; // Special Finder Sauce
};

extern struct MFImpl MF1, MF2, MF3;

struct MFImpl MFChoose(const char *suggest);

struct MFImpl {
	const char *Name;
	int (*Init)(void);
	int (*Open)(void *opaque, short refnum, uint32_t fid, const char *name, bool resfork, bool write);
	int (*Close)(void *opaque);
	int (*Read)(void *opaque, void *buf, uint64_t offset, uint32_t count, uint32_t *actual_count);
	int (*Write)(void *opaque, const void *buf, uint64_t offset, uint32_t count, uint32_t *actual_count);
	int (*SetEOF)(void *opaque, uint64_t len);
	int (*FGetAttr)(uint32_t fid, const char *name, unsigned fields, struct MFAttr *attr);
	int (*FSetAttr)(uint32_t fid, const char *name, unsigned fields, const struct MFAttr *attr);
	int (*DGetAttr)(uint32_t fid, const char *name, unsigned fields, struct MFAttr *attr);
	int (*DSetAttr)(uint32_t fid, const char *name, unsigned fields, const struct MFAttr *attr);
	int (*Move)(uint32_t fid1, const char *name1, uint32_t fid2, const char *name2);
	int (*Del)(uint32_t fid, const char *name, bool isdir);
	bool (*IsSidecar)(const char *name);
};
