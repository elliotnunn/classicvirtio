/* Copyright (c) 2024 Elliot Nunn */
/* Licensed under the MIT license */

#include <LowMem.h>
#include <Memory.h>

#include <stdbool.h>

#include "9p.h"
#include "panic.h"

#include "9buf.h"

/********************************** READING **********************************/

static uint32_t rfid;
static bool rbufok; // todo: make this redundant with a nonsense rbufat value
static char *rbuf, *rborrow;
static int32_t rbufsize;
static int32_t rbufat, rseek;

void SetRead(uint32_t fid, void *buffer, int32_t buflen) {
	rfid = fid;
	rbufok = false;
	rborrow = NULL;
	rbuf = buffer;
	rbufsize = buflen;
	rbufat = rseek = 0;
}

void RSeek(int32_t to) {
	if (rborrow) panic("RSeek before RBuffer giveback");
	rseek = to;
}

int32_t RTell(void) {
	return rseek;
}

// "Borrow" a pointer into a contiguous chunk of the buffer, and/or
// giveback a previously borrowed pointer, plus the number of bytes consumed.
// (file gets null terminated)
char *RBuffer(char *giveback, size_t min) {
	// Fast path: plenty of room left in this buffer
	if (rbufok && giveback && min!=0 && giveback+min <= rbuf+rbufsize) {
		return giveback;
	}

	// Seek forward (by comparing the passed-in ptr to the last one we returned)
	if (giveback) {
		if (!rborrow) panic("RBuffer giveback without borrowing\n");
		rseek += giveback - rborrow;
	}

	// Calls that seek but don't otherwise require further data
	if (min == 0) {
		return rborrow = NULL;
	}

	// Try to satisfy the entire request from the buffer
	if (rbufok && rseek>=rbufat && rseek+min<rbufat+rbufsize) {
		return rborrow = rbuf + (rseek-rbufat);
	}

	// Instead try to salvage some bytes from the buffer, move them left
	size_t salvaged = 0;
	if (rbufok && rbufat+rbufsize > rseek) {
		salvaged = rbufsize - (rseek - rbufat);
		BlockMove(rbuf + (rseek - rbufat), rbuf, salvaged);
	}

	// Make an expensive Tread call
	int32_t gotten;
	Read9(rfid, rbuf+salvaged, rseek+salvaged, rbufsize-salvaged, &gotten);
	if (salvaged+gotten < rbufsize) rbuf[salvaged+gotten] = 0; // null-term EOF

	rbufok = true;
	rbufat = rseek;
	return rborrow = rbuf;
}

/********************************** WRITING **********************************/

// Limited to writing contiguously from the start of the file,
// "Rewrite" being the only exception

static uint32_t wfid;
static char *wbuf, *wborrow;
static int32_t wseek, wbufsize, wbufat;

void SetWrite(uint32_t fid, void *buffer, int32_t buflen) {
	wfid = fid;
	wborrow = NULL;
	wbuf = buffer;
	wbufsize = buflen;
	wbufat = wseek = 0;
}

int32_t WTell(void) {
	return wseek;
}

// "Borrow" a pointer into a contiguous chunk of the buffer, and/or
// giveback a previously borrowed pointer, plus the number of bytes produced.
char *WBuffer(char *giveback, int32_t min) {
	// Fast path: plenty of room left in this buffer
	if (giveback && min!=0 && giveback+min <= wbuf+wbufsize) {
		return giveback;
	}

	// Seek forward (by comparing the passed-in ptr to the last one we returned)
	if (giveback) {
		if (!wborrow) panic("WBuffer giveback without borrowing\n");
		wseek += giveback - wborrow;
	}

	// Calls that seek but don't otherwise require further data
	if (min == 0) {
		return wborrow = NULL;
	}

	// Try to satisfy the request from the buffer
	if (wseek+min<wbufat+wbufsize) {
		return wborrow = wbuf + (wseek-wbufat);
	}

	WFlush();
	return wborrow = wbuf;
}

void WFlush(void) {
	if (wseek > wbufat) {
		if (Write9(wfid, wbuf, wbufat, wseek-wbufat, NULL)) {
			panic("WFlush Twrite failed");
		}
	}
	wbufat = wseek;
}

// Special case for resource forks:
// the only way to rewrite bytes that were already written with WBuffer
void Rewrite(void *buf, int32_t at, int32_t cnt) {
	// Happy case: edit data still in buffer
	int32_t ok = 0;
	for (int32_t i=0; i<cnt; i++) {
		if (at+i >= wbufat && at+i < wseek) {
			wbuf[at+i-wbufat] = ((char *)buf)[i];
			ok++;
		}
	}

	// Sad case: need an expensive syscall because bytes already flushed
	if (ok < cnt) {
		if (Write9(wfid, buf, at, cnt, NULL)) {
			panic("Rewrite Twrite failed");
		}
	}
}
