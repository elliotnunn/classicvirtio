/* Copyright (c) 2023 Elliot Nunn */
/* Licensed under the MIT license */

// Header-only library

#pragma once

#if GENERATINGCFM

// In the NDRV runtime use CallSecondaryInterruptHandler2
// (must not be called from hardware interrupt!)

// We take liberties with casting function pointers,
// because we know PowerPC's register calling convention.

#include <DriverServices.h>

static long ATOMICWRAPPER(void (*func)(void *a1, void *a2, void *a3, void *a4, void *a5, void *a6, void *a7, void *a8), void **args) {
	func(args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7]);
	return 0;
}

#define ATOMIC(func) CallSecondaryInterruptHandler2((void *)(func), NULL, NULL, NULL)
#define ATOMIC1(func, a1) CallSecondaryInterruptHandler2((void *)(func), NULL, (void *)(long)(a1), NULL)
#define ATOMIC2(func, a1, a2) CallSecondaryInterruptHandler2((void *)(func), NULL, (void *)(long)(a1), (void *)(long)(a2))
#define ATOMIC3(func, a1, a2, a3) CallSecondaryInterruptHandler2(ATOMICWRAPPER, NULL, func, \
	(void *[]){(void *)(long)a1, (void *)(long)a2, (void *)(long)a3});
#define ATOMIC4(func, a1, a2, a3, a4) CallSecondaryInterruptHandler2(ATOMICWRAPPER, NULL, func, \
	(void *[]){(void *)(long)a1, (void *)(long)a2, (void *)(long)a3, (void *)(long)a4});
#define ATOMIC5(func, a1, a2, a3, a4, a5) CallSecondaryInterruptHandler2(ATOMICWRAPPER, NULL, func, \
	(void *[]){(void *)(long)a1, (void *)(long)a2, (void *)(long)a3, (void *)(long)a4, (void *)(long)a5});
#define ATOMIC6(func, a1, a2, a3, a4, a5, a6) CallSecondaryInterruptHandler2(ATOMICWRAPPER, NULL, func, \
	(void *[]){(void *)(long)a1, (void *)(long)a2, (void *)(long)a3, (void *)(long)a4, (void *)(long)a5, (void *)(long)a6});
#define ATOMIC7(func, a1, a2, a3, a4, a5, a6, a7) CallSecondaryInterruptHandler2(ATOMICWRAPPER, NULL, func, \
	(void *[]){(void *)(long)a1, (void *)(long)a2, (void *)(long)a3, (void *)(long)a4, (void *)(long)a5, (void *)(long)a6, (void *)(long)a7});
#define ATOMIC8(func, a1, a2, a3, a4, a5, a6, a7, a8) CallSecondaryInterruptHandler2(ATOMICWRAPPER, NULL, func, \
	(void *[]){(void *)(long)a1, (void *)(long)a2, (void *)(long)a3, (void *)(long)a4, (void *)(long)a5, (void *)(long)a6, (void *)(long)a7, (void *)(long)a8});

#else

// In the DRVR runtime fiddle the SR interrupt mask and call the function directly

// Better to turn these into modern GCC inline assembly
#pragma parameter __D0 IntsOff
short IntsOff(void) = {0x40c0, 0x007c, 0x0700}; // save sr and mask

#pragma parameter IntsOn(__D0)
void IntsOn(short) = {0x46c0};

#define ATOMIC(func) {short _sr_ = IntsOff(); func(); IntsOn(_sr_);}
#define ATOMIC1(func, a1) {short _sr_ = IntsOff(); func(a1); IntsOn(_sr_);}
#define ATOMIC2(func, a1, a2) {short _sr_ = IntsOff(); func(a1, a2); IntsOn(_sr_);}
#define ATOMIC3(func, a1, a2, a3) {short _sr_ = IntsOff(); func(a1, a2, a3); IntsOn(_sr_);}
#define ATOMIC4(func, a1, a2, a3, a4) {short _sr_ = IntsOff(); func(a1, a2, a3, a4); IntsOn(_sr_);}
#define ATOMIC5(func, a1, a2, a3, a4, a5) {short _sr_ = IntsOff(); func(a1, a2, a3, a4, a5); IntsOn(_sr_);}
#define ATOMIC6(func, a1, a2, a3, a4, a5, a) {short _sr_ = IntsOff(); func(a1, a2, a3, a4, a5, a6); IntsOn(_sr_);}
#define ATOMIC7(func, a1, a2, a3, a4, a5, a6, a7) {short _sr_ = IntsOff(); func(a1, a2, a3, a4, a5, a6, a7); IntsOn(_sr_);}
#define ATOMIC8(func, a1, a2, a3, a4, a5, a6, a7, a8) {short _sr_ = IntsOff(); func(a1, a2, a3, a4, a5, a6, a7, a8); IntsOn(_sr_);}

#endif
