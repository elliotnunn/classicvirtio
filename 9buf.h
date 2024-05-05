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

void SetRead(uint32_t fid, void *buffer, uint32_t buflen);
static int Peek(void);
static int Read(void);
static bool ReadIf(char want);

void SetWrite(uint32_t fid, void *buffer, uint32_t buflen);
int WriteBuf(void *x, size_t n);
static int Write(char x);
int Overwrite(void *buf, uint64_t at, uint32_t cnt); // for resource forks specifically
int Flush(void);

// Feel free to play with these globals
extern char *rbuf;
extern uint32_t rbufsize, rbufcnt;
extern uint64_t rbufat, rbufseek;
extern uint32_t rfid;

extern char *wbuf;
extern uint32_t wbufsize, wbufcnt;
extern uint64_t wbufat, wbufseek;
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
