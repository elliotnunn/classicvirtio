/* Copyright (c) 2024 Elliot Nunn */
/* Licensed under the MIT license */

// Header-only buffered reading and writing via 9P layer
// Narrow in application -- used by Rez layer
// Caller supplies buffer, so be careful of stack-based buffers
// There is one file open for reading, and one for writing
// Some functions in the header for inlining

#include "9p.h"

#include <stdint.h>
#include <string.h>

void SetRead(uint32_t fid, void *buffer, long buflen);
static int Peek(void);
static int Read(void);
static bool ReadIf(char want);

void SetWrite(uint32_t fid, void *buffer, long buflen);
int WriteBuf(void *x, size_t n);
static int Write(char x);
int Overwrite(void *buf, long at, long cnt); // for resource forks specifically
int Flush(void);

// Feel free to play with these globals
extern char *rbuf;
extern long rbufsize, rbufat, rbufcnt, rbufseek;
extern uint32_t rfid;

extern char *wbuf;
extern long wbufsize, wbufat, wbufcnt, wbufseek;
extern uint32_t wfid;

static int Peek(void) {
	// Not satisfiable from cache
	if (rbufseek < rbufat || rbufseek >= rbufat+rbufcnt) {
		uint32_t got;
		Read9(rfid, rbuf, rbufseek, rbufsize, &got);
		rbufat = rbufseek;
		rbufcnt = got;
		if (got == 0) return -1; // error, probably eof
	}

	return 0xff & rbuf[rbufseek-rbufat];
}

static int Read(void) {
	int c = Peek();
	if (c != -1) rbufseek++;
	return c;
}

static bool ReadIf(char want) {
	int c = Peek();
	if (c == want) {
		rbufseek++;
		return true;
	} else {
		return false;
	}
}

static int Write(char x) {
	if (wbufcnt >= wbufsize) {
		int err = Flush();
		if (err) return err;
	}

	wbuf[wbufcnt] = x;
	wbufcnt++;
	wbufseek++;
	return 0;
}
