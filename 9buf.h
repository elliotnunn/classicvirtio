/* Copyright (c) 2024 Elliot Nunn */
/* Licensed under the MIT license */

// Buffered reading and writing via 9P layer
// Narrow in application -- used by Rez layer
// Caller supplies buffer, so be careful of stack-based buffers
// There is one file open for reading, and one for writing

#pragma once

#include <stdint.h>

void SetRead(uint32_t fid, void *buffer, int32_t buflen);
void RSeek(int32_t to);
int32_t RTell(void);
char *RBuffer(char *giveback, int32_t min);

void SetWrite(uint32_t fid, void *buffer, int32_t buflen);
int32_t WTell(void);
char *WBuffer(char *giveback, int32_t min);
void WFlush(void);
void Rewrite(void *buf, int32_t at, int32_t cnt);
