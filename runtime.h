/* Copyright (c) Elliot Nunn */
/* Licensed under the MIT license */

// Paper over differences between Classic DRVR and PowerPC NDRV
// These routines are implemented by device-whatever.c and called by runtime-whatever.c

// Under the Classic Device Manager, a driver's lifetime is bounded by Open and Close calls.
// Under the Native Device Manager, there are Initialize/Replace and Finalize/Superseded;
// Open and Close still exist but Mac OS does not give them "any specific tasks".
// Here we generalise these to new terms: Start and Stop.

#pragma once

#include <Files.h>

int DriverStart(short refNum); // DRVR Open or NDRV Initialize
int DriverRead(IOParam *pb);
int DriverWrite(IOParam *pb);
int DriverCtl(CntrlParam *pb);
int DriverStatus(CntrlParam *pb);
int DriverStop(void); // DRVR Close or NDRV Finalize
