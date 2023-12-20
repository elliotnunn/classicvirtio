/* Copyright (c) 2023 Elliot Nunn */
/* Licensed under the MIT license */

// File System Manager functions (UT_*) to access the FCB table may be:
// unavailable and unnecessary (-7.1, or System file hasn't loaded yet)
// available but unnecessary (7.5-8.6)
// available and necessary (9.0-)
// And the accessors weren't in InterfaceLib until 8.5!
// So we must polyfill them.

#pragma once

OSErr UnivAllocateFCB(short *fileRefNum, FCBRecPtr *fileCtrlBlockPtr);
OSErr UnivResolveFCB(short fileRefNum, FCBRecPtr *fileCtrlBlockPtr);
OSErr UnivIndexFCB(VCBPtr volCtrlBlockPtr, short *fileRefNum, FCBRecPtr *fileCtrlBlockPtr);
