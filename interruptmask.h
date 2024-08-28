/* Copyright (c) 2023 Elliot Nunn */
/* Licensed under the MIT license */

/*
Previously we depended on CallSecondaryInterruptHandler2,
but it would never have worked to make a VM-compatible disk.
*/

#pragma once
#include <stdbool.h>
bool Interruptible(short sr);
short DisableInterrupts(void);
void ReenableInterrupts(short oldlevel);
void ReenableInterruptsAndWaitFor(short oldlevel, volatile unsigned long *flag);
