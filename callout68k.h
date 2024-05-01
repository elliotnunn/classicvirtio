/* Copyright (c) 2023 Elliot Nunn */
/* Licensed under the MIT license */

#pragma once

// Use some modern GCC constructs to make calling 68k procedures less of a pain

// A better macro than Interfaces' SIZE_CODE() for building in a Mixed Mode routine descriptor,
// because it correctly returns zero as the code for void
#define _MMTYPESIZE_(T) (__builtin_types_compatible_p(T, void) ? 0 : sizeof(T)>=4 ? 3 : sizeof(T))

#if GENERATINGCFM // PowerPC or CFM-68k

#define CALL0(ret, proc) \
	(ret)CallUniversalProc((void *)proc, \
		kCStackBased \
		| RESULT_SIZE(_MMTYPESIZE_(ret)))

#define CALL1(ret, proc, t1, v1) \
	(ret)CallUniversalProc((void *)proc, \
		kCStackBased \
		| RESULT_SIZE(_MMTYPESIZE_(ret)) \
		| STACK_ROUTINE_PARAMETER(1, _MMTYPESIZE_(t1)), \
		v1)

#define CALL2(ret, proc, t1, v1, t2, v2) \
	(ret)CallUniversalProc((void *)proc, \
		kCStackBased \
		| RESULT_SIZE(_MMTYPESIZE_(ret)) \
		| STACK_ROUTINE_PARAMETER(1, _MMTYPESIZE_(t1)) \
		| STACK_ROUTINE_PARAMETER(2, _MMTYPESIZE_(t2)), \
		v1, v2)

#define CALL3(ret, proc, t1, v1, t2, v2, t3, v3) \
	(ret)CallUniversalProc((void *)proc, \
		kCStackBased \
		| RESULT_SIZE(_MMTYPESIZE_(ret)) \
		| STACK_ROUTINE_PARAMETER(1, _MMTYPESIZE_(t1)) \
		| STACK_ROUTINE_PARAMETER(2, _MMTYPESIZE_(t2)) \
		| STACK_ROUTINE_PARAMETER(3, _MMTYPESIZE_(t3)), \
		v1, v2, v3)

#define CALL4(ret, proc, t1, v1, t2, v2, t3, v3, t4, v4) \
	(ret)CallUniversalProc((void *)proc, \
		kCStackBased \
		| RESULT_SIZE(_MMTYPESIZE_(ret)) \
		| STACK_ROUTINE_PARAMETER(1, _MMTYPESIZE_(t1)) \
		| STACK_ROUTINE_PARAMETER(2, _MMTYPESIZE_(t2)) \
		| STACK_ROUTINE_PARAMETER(3, _MMTYPESIZE_(t3)) \
		| STACK_ROUTINE_PARAMETER(4, _MMTYPESIZE_(t4)), \
		v1, v2, v3, v4)

#define CALL5(ret, proc, t1, v1, t2, v2, t3, v3, t4, v4, t5, v5) \
	(ret)CallUniversalProc((void *)proc, \
		kCStackBased \
		| RESULT_SIZE(_MMTYPESIZE_(ret)) \
		| STACK_ROUTINE_PARAMETER(1, _MMTYPESIZE_(t1)) \
		| STACK_ROUTINE_PARAMETER(2, _MMTYPESIZE_(t2)) \
		| STACK_ROUTINE_PARAMETER(3, _MMTYPESIZE_(t3)) \
		| STACK_ROUTINE_PARAMETER(4, _MMTYPESIZE_(t4)) \
		| STACK_ROUTINE_PARAMETER(5, _MMTYPESIZE_(t5)), \
		v1, v2, v3, v4, v5)

#define CALL6(ret, proc, t1, v1, t2, v2, t3, v3, t4, v4, t5, v5, t6, v6) \
	(ret)CallUniversalProc((void *)proc, \
		kCStackBased \
		| RESULT_SIZE(_MMTYPESIZE_(ret)) \
		| STACK_ROUTINE_PARAMETER(1, _MMTYPESIZE_(t1)) \
		| STACK_ROUTINE_PARAMETER(2, _MMTYPESIZE_(t2)) \
		| STACK_ROUTINE_PARAMETER(3, _MMTYPESIZE_(t3)) \
		| STACK_ROUTINE_PARAMETER(4, _MMTYPESIZE_(t4)) \
		| STACK_ROUTINE_PARAMETER(5, _MMTYPESIZE_(t5)) \
		| STACK_ROUTINE_PARAMETER(6, _MMTYPESIZE_(t6)), \
		v1, v2, v3, v4, v5, v6)

#define CALLPASCAL0(ret, proc) \
	(ret)CallUniversalProc((void *)proc, \
		kPascalStackBased \
		| RESULT_SIZE(_MMTYPESIZE_(ret)))

#define CALLPASCAL1(ret, proc, t1, v1) \
	(ret)CallUniversalProc((void *)proc, \
		kPascalStackBased \
		| RESULT_SIZE(_MMTYPESIZE_(ret)) \
		| STACK_ROUTINE_PARAMETER(1, _MMTYPESIZE_(t1)), \
		v1)

#define CALLPASCAL2(ret, proc, t1, v1, t2, v2) \
	(ret)CallUniversalProc((void *)proc, \
		kPascalStackBased \
		| RESULT_SIZE(_MMTYPESIZE_(ret)) \
		| STACK_ROUTINE_PARAMETER(1, _MMTYPESIZE_(t1)) \
		| STACK_ROUTINE_PARAMETER(2, _MMTYPESIZE_(t2)), \
		v1, v2)

#define CALLPASCAL3(ret, proc, t1, v1, t2, v2, t3, v3) \
	(ret)CallUniversalProc((void *)proc, \
		kPascalStackBased \
		| RESULT_SIZE(_MMTYPESIZE_(ret)) \
		| STACK_ROUTINE_PARAMETER(1, _MMTYPESIZE_(t1)) \
		| STACK_ROUTINE_PARAMETER(2, _MMTYPESIZE_(t2)) \
		| STACK_ROUTINE_PARAMETER(3, _MMTYPESIZE_(t3)), \
		v1, v2, v3)

#define CALLPASCAL4(ret, proc, t1, v1, t2, v2, t3, v3, t4, v4) \
	(ret)CallUniversalProc((void *)proc, \
		kPascalStackBased \
		| RESULT_SIZE(_MMTYPESIZE_(ret)) \
		| STACK_ROUTINE_PARAMETER(1, _MMTYPESIZE_(t1)) \
		| STACK_ROUTINE_PARAMETER(2, _MMTYPESIZE_(t2)) \
		| STACK_ROUTINE_PARAMETER(3, _MMTYPESIZE_(t3)) \
		| STACK_ROUTINE_PARAMETER(4, _MMTYPESIZE_(t4)), \
		v1, v2, v3, v4)

#define CALLPASCAL5(ret, proc, t1, v1, t2, v2, t3, v3, t4, v4, t5, v5) \
	(ret)CallUniversalProc((void *)proc, \
		kPascalStackBased \
		| RESULT_SIZE(_MMTYPESIZE_(ret)) \
		| STACK_ROUTINE_PARAMETER(1, _MMTYPESIZE_(t1)) \
		| STACK_ROUTINE_PARAMETER(2, _MMTYPESIZE_(t2)) \
		| STACK_ROUTINE_PARAMETER(3, _MMTYPESIZE_(t3)) \
		| STACK_ROUTINE_PARAMETER(4, _MMTYPESIZE_(t4)) \
		| STACK_ROUTINE_PARAMETER(5, _MMTYPESIZE_(t5)), \
		v1, v2, v3, v4, v5)

#define CALLPASCAL6(ret, proc, t1, v1, t2, v2, t3, v3, t4, v4, t5, v5, t6, v6) \
	(ret)CallUniversalProc((void *)proc, \
		kPascalStackBased \
		| RESULT_SIZE(_MMTYPESIZE_(ret)) \
		| STACK_ROUTINE_PARAMETER(1, _MMTYPESIZE_(t1)) \
		| STACK_ROUTINE_PARAMETER(2, _MMTYPESIZE_(t2)) \
		| STACK_ROUTINE_PARAMETER(3, _MMTYPESIZE_(t3)) \
		| STACK_ROUTINE_PARAMETER(4, _MMTYPESIZE_(t4)) \
		| STACK_ROUTINE_PARAMETER(5, _MMTYPESIZE_(t5)) \
		| STACK_ROUTINE_PARAMETER(6, _MMTYPESIZE_(t6)), \
		v1, v2, v3, v4, v5, v6)

#else // classic 68k

#define CALL0(ret, proc) \
	((typeof(ret (*)(void)))proc)()

#define CALL1(ret, proc, t1, v1) \
	((typeof(ret (*)(t1)))proc)(v1)

#define CALL2(ret, proc, t1, v1, t2, v2) \
	((typeof(ret (*)(t1, t2)))proc)(v1, v2)

#define CALL3(ret, proc, t1, v1, t2, v2, t3, v3) \
	((typeof(ret (*)(t1, t2, t3)))proc)(v1, v2, v3)

#define CALL4(ret, proc, t1, v1, t2, v2, t3, v3, t4, v4) \
	((typeof(ret (*)(t1, t2, t3, t4)))proc)(v1, v2, v3, v4)

#define CALL5(ret, proc, t1, v1, t2, v2, t3, v3, t4, v4, t5, v5) \
	((typeof(ret (*)(t1, t2, t3, t4, t5)))proc)(v1, v2, v3, v4, v5)

#define CALL6(ret, proc, t1, v1, t2, v2, t3, v3, t4, v4, t5, v5, t6, v6) \
	((typeof(ret (*)(t1, t2, t3, t4, t5, t6)))proc)(v1, v2, v3, v4, v5, v6)

#define CALLPASCAL0(ret, proc) \
	((typeof(pascal ret (*)(void)))proc)()

#define CALLPASCAL1(ret, proc, t1, v1) \
	((typeof(pascal ret (*)(t1)))proc)(v1)

#define CALLPASCAL2(ret, proc, t1, v1, t2, v2) \
	((typeof(pascal ret (*)(t1, t2)))proc)(v1, v2)

#define CALLPASCAL3(ret, proc, t1, v1, t2, v2, t3, v3) \
	((typeof(pascal ret (*)(t1, t2, t3)))proc)(v1, v2, v3)

#define CALLPASCAL4(ret, proc, t1, v1, t2, v2, t3, v3, t4, v4) \
	((typeof(pascal ret (*)(t1, t2, t3, t4)))proc)(v1, v2, v3, v4)

#define CALLPASCAL5(ret, proc, t1, v1, t2, v2, t3, v3, t4, v4, t5, v5) \
	((typeof(pascal ret (*)(t1, t2, t3, t4, t5)))proc)(v1, v2, v3, v4, v5)

#define CALLPASCAL6(ret, proc, t1, v1, t2, v2, t3, v3, t4, v4, t5, v5, t6, v6) \
	((typeof(pascal ret (*)(t1, t2, t3, t4, t5, t6)))proc)(v1, v2, v3, v4, v5, v6)

#endif
