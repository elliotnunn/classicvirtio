/* Copyright (c) 2024 Elliot Nunn */
/* Licensed under the MIT license */

#include <LowMem.h>
#include <Memory.h>

#include <stdbool.h>

#include "9p.h"
#include "panic.h"

#include "9buf.h"

/********************************** READING **********************************/

// Can seek anywhere in a file, and will null-terminate the file

static uint32_t rfid;
static char *rbuf, *rborrow;
static int32_t rbufsize;
static int32_t rbufat, rseek;

void SetRead(uint32_t fid, void *buffer, int32_t buflen) {
	rfid = fid;
	rborrow = NULL;
	rbuf = buffer;
	rbufsize = buflen;
	rseek = 0;
	rbufat = INT32_MIN; // poison value
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
char *RBuffer(char *giveback, int32_t min) {
	// Fast path: plenty of room left in this buffer
	if (giveback && min!=0 && giveback+min <= rbuf+rbufsize) {
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

	bool firstByteOK = rbufat<=rseek && rseek<rbufat+rbufsize;
	bool lastByteOK = rbufat<=rseek+min-1 && rseek+min-1<rbufat+rbufsize;

	char *getptr;
	int32_t keep, get, getoffset;
	if (firstByteOK && lastByteOK) {
		// Satisfy whole request from buffer
		return rborrow = rbuf+rseek-rbufat;
	} else if (firstByteOK) {
		// Move buffer bytes to lower addresses and repopulate higher addresses
		keep = rbufat+rbufsize-rseek;
		get = rseek-rbufat;
		BlockMoveData(rbuf+get, rbuf, keep);
		getptr = rbuf+keep;
		getoffset = rseek+keep;
	} else if (lastByteOK) {
		// Move buffer bytes to higher addresses and repopulate lower addresses
		keep = rseek+rbufsize-rbufat;
		get = rbufat-rseek;
		BlockMoveData(rbuf, rbuf+get, keep);
		getptr = rbuf;
		getoffset = rseek;
	} else {
		get = rbufsize;
		getptr = rbuf;
		getoffset = rseek;
	}

	// Make an expensive Tread call
	int32_t gotten;
	Read9(rfid, getptr, getoffset, get, &gotten);
	if (gotten < get) getptr[gotten] = 0; // null-term EOF

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
