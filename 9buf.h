/* Copyright (c) 2024 Elliot Nunn */
/* Licensed under the MIT license */

// Buffered reading and writing via 9P layer
// Narrow in application -- used by Rez layer
// Caller supplies buffer, so be careful of stack-based buffers
// There is one file open for reading, and one for writing

#pragma once

#include <stdbool.h>
#include <stdint.h>

void SetRead(uint32_t fid, void *buffer, uint32_t buflen);
void RSeek(uint64_t to);
uint64_t RTell(void);
char *RBuffer(char *giveback, size_t min);

void SetWrite(uint32_t fid, void *buffer, uint32_t buflen);
void WriteBuf(void *x, size_t n);
static void Write(char x);
void Overwrite(void *buf, uint64_t at, uint32_t cnt); // for resource forks specifically
void Flush(void);
char *BorrowWriteBuf(size_t min);
void ReturnWriteBuf(char *borrowed);

// Feel free to play with these globals
extern char *wbuf;
extern uint32_t wbufsize, wbufcnt;
extern uint64_t wbufat, wbufseek;
extern uint32_t wfid;

extern long bufDiskTime;

static void Write(char x) {
	if (wbufcnt >= wbufsize) {
		Flush();
	}

	wbuf[wbufcnt] = x;
	wbufcnt++;
	wbufseek++;
}
