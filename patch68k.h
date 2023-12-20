/* Copyright (c) 2023 Elliot Nunn */
/* Licensed under the MIT license */

#pragma once

#define ORIGCODE68K "%o" // no arguments: 32-bit address of original routine
#define BYTECODE68K "%b" // 1 argument: 8-bit literal
#define WORDCODE68K "%w" // 1 argument: 16-bit literal
#define LONGCODE68K "%l" // 1 argument: 32-bit literal
// "%[A-Z]+" (capital) is a byte-sized reference to a label
// "[A-Z]+" is the label
// but only the first letter is significant!
// ... and everything else in the format string is hex or whitespace

// If the code "falls through" the end, it will run an uninstaller routine,
// then return.

void *Patch68k(unsigned long vector, const char *fmt, ...);
void Unpatch68k(void *patch);
