/* Copyright (c) 2023 Elliot Nunn */
/* Licensed under the MIT license */

#pragma once

// Use some modern GCC constructs to make calling 68k procedures less of a pain
// Get away with sizeof(void) being 1 and not 0

#if GENERATINGCFM // PowerPC or CFM-68k

#define CALL0(ret, proc) \
	(ret)CallUniversalProc((void *)proc, \
		kCStackBased \
		| RESULT_SIZE(SIZE_CODE(sizeof(ret))))

#define CALL1(ret, proc, t1, v1) \
	(ret)CallUniversalProc((void *)proc, \
		kCStackBased \
		| RESULT_SIZE(SIZE_CODE(sizeof(ret))) \
		| STACK_ROUTINE_PARAMETER(1, SIZE_CODE(sizeof(t1))), \
		v1)

#define CALL2(ret, proc, t1, v1, t2, v2) \
	(ret)CallUniversalProc((void *)proc, \
		kCStackBased \
		| RESULT_SIZE(SIZE_CODE(sizeof(ret))) \
		| STACK_ROUTINE_PARAMETER(1, SIZE_CODE(sizeof(t1))) \
		| STACK_ROUTINE_PARAMETER(2, SIZE_CODE(sizeof(t2))), \
		v1, v2)

#define CALL3(ret, proc, t1, v1, t2, v2, t3, v3) \
	(ret)CallUniversalProc((void *)proc, \
		kCStackBased \
		| RESULT_SIZE(SIZE_CODE(sizeof(ret))) \
		| STACK_ROUTINE_PARAMETER(1, SIZE_CODE(sizeof(t1))) \
		| STACK_ROUTINE_PARAMETER(2, SIZE_CODE(sizeof(t2))) \
		| STACK_ROUTINE_PARAMETER(3, SIZE_CODE(sizeof(t3))), \
		v1, v2, v3)

#define CALL4(ret, proc, t1, v1, t2, v2, t3, v3, t4, v4) \
	(ret)CallUniversalProc((void *)proc, \
		kCStackBased \
		| RESULT_SIZE(SIZE_CODE(sizeof(ret))) \
		| STACK_ROUTINE_PARAMETER(1, SIZE_CODE(sizeof(t1))) \
		| STACK_ROUTINE_PARAMETER(2, SIZE_CODE(sizeof(t2))) \
		| STACK_ROUTINE_PARAMETER(3, SIZE_CODE(sizeof(t3))) \
		| STACK_ROUTINE_PARAMETER(4, SIZE_CODE(sizeof(t4))), \
		v1, v2, v3, v4)

#define CALL5(ret, proc, t1, v1, t2, v2, t3, v3, t4, v4, t5, v5) \
	(ret)CallUniversalProc((void *)proc, \
		kCStackBased \
		| RESULT_SIZE(SIZE_CODE(sizeof(ret))) \
		| STACK_ROUTINE_PARAMETER(1, SIZE_CODE(sizeof(t1))) \
		| STACK_ROUTINE_PARAMETER(2, SIZE_CODE(sizeof(t2))) \
		| STACK_ROUTINE_PARAMETER(3, SIZE_CODE(sizeof(t3))) \
		| STACK_ROUTINE_PARAMETER(4, SIZE_CODE(sizeof(t4))) \
		| STACK_ROUTINE_PARAMETER(5, SIZE_CODE(sizeof(t5))), \
		v1, v2, v3, v4, v5)

#define CALL6(ret, proc, t1, v1, t2, v2, t3, v3, t4, v4, t5, v5, t6, v6) \
	(ret)CallUniversalProc((void *)proc, \
		kCStackBased \
		| RESULT_SIZE(SIZE_CODE(sizeof(ret))) \
		| STACK_ROUTINE_PARAMETER(1, SIZE_CODE(sizeof(t1))) \
		| STACK_ROUTINE_PARAMETER(2, SIZE_CODE(sizeof(t2))) \
		| STACK_ROUTINE_PARAMETER(3, SIZE_CODE(sizeof(t3))) \
		| STACK_ROUTINE_PARAMETER(4, SIZE_CODE(sizeof(t4))) \
		| STACK_ROUTINE_PARAMETER(5, SIZE_CODE(sizeof(t5))) \
		| STACK_ROUTINE_PARAMETER(6, SIZE_CODE(sizeof(t6))), \
		v1, v2, v3, v4, v5, v6)

#define CALLPASCAL0(ret, proc) \
	(ret)CallUniversalProc((void *)proc, \
		kPascalStackBased \
		| RESULT_SIZE(SIZE_CODE(sizeof(ret))))

#define CALLPASCAL1(ret, proc, t1, v1) \
	(ret)CallUniversalProc((void *)proc, \
		kPascalStackBased \
		| RESULT_SIZE(SIZE_CODE(sizeof(ret))) \
		| STACK_ROUTINE_PARAMETER(1, SIZE_CODE(sizeof(t1))), \
		v1)

#define CALLPASCAL2(ret, proc, t1, v1, t2, v2) \
	(ret)CallUniversalProc((void *)proc, \
		kPascalStackBased \
		| RESULT_SIZE(SIZE_CODE(sizeof(ret))) \
		| STACK_ROUTINE_PARAMETER(1, SIZE_CODE(sizeof(t1))) \
		| STACK_ROUTINE_PARAMETER(2, SIZE_CODE(sizeof(t2))), \
		v1, v2)

#define CALLPASCAL3(ret, proc, t1, v1, t2, v2, t3, v3) \
	(ret)CallUniversalProc((void *)proc, \
		kPascalStackBased \
		| RESULT_SIZE(SIZE_CODE(sizeof(ret))) \
		| STACK_ROUTINE_PARAMETER(1, SIZE_CODE(sizeof(t1))) \
		| STACK_ROUTINE_PARAMETER(2, SIZE_CODE(sizeof(t2))) \
		| STACK_ROUTINE_PARAMETER(3, SIZE_CODE(sizeof(t3))), \
		v1, v2, v3)

#define CALLPASCAL4(ret, proc, t1, v1, t2, v2, t3, v3, t4, v4) \
	(ret)CallUniversalProc((void *)proc, \
		kPascalStackBased \
		| RESULT_SIZE(SIZE_CODE(sizeof(ret))) \
		| STACK_ROUTINE_PARAMETER(1, SIZE_CODE(sizeof(t1))) \
		| STACK_ROUTINE_PARAMETER(2, SIZE_CODE(sizeof(t2))) \
		| STACK_ROUTINE_PARAMETER(3, SIZE_CODE(sizeof(t3))) \
		| STACK_ROUTINE_PARAMETER(4, SIZE_CODE(sizeof(t4))), \
		v1, v2, v3, v4)

#define CALLPASCAL5(ret, proc, t1, v1, t2, v2, t3, v3, t4, v4, t5, v5) \
	(ret)CallUniversalProc((void *)proc, \
		kPascalStackBased \
		| RESULT_SIZE(SIZE_CODE(sizeof(ret))) \
		| STACK_ROUTINE_PARAMETER(1, SIZE_CODE(sizeof(t1))) \
		| STACK_ROUTINE_PARAMETER(2, SIZE_CODE(sizeof(t2))) \
		| STACK_ROUTINE_PARAMETER(3, SIZE_CODE(sizeof(t3))) \
		| STACK_ROUTINE_PARAMETER(4, SIZE_CODE(sizeof(t4))) \
		| STACK_ROUTINE_PARAMETER(5, SIZE_CODE(sizeof(t5))), \
		v1, v2, v3, v4, v5)

#define CALLPASCAL6(ret, proc, t1, v1, t2, v2, t3, v3, t4, v4, t5, v5, t6, v6) \
	(ret)CallUniversalProc((void *)proc, \
		kPascalStackBased \
		| RESULT_SIZE(SIZE_CODE(sizeof(ret))) \
		| STACK_ROUTINE_PARAMETER(1, SIZE_CODE(sizeof(t1))) \
		| STACK_ROUTINE_PARAMETER(2, SIZE_CODE(sizeof(t2))) \
		| STACK_ROUTINE_PARAMETER(3, SIZE_CODE(sizeof(t3))) \
		| STACK_ROUTINE_PARAMETER(4, SIZE_CODE(sizeof(t4))) \
		| STACK_ROUTINE_PARAMETER(5, SIZE_CODE(sizeof(t5))) \
		| STACK_ROUTINE_PARAMETER(6, SIZE_CODE(sizeof(t6))), \
		v1, v2, v3, v4, v5, v6)

#define STATICDESCRIPTOR(func, info) ({static RoutineDescriptor rd = BUILD_ROUTINE_DESCRIPTOR(info, func); (void *)&rd;})

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

#define STATICDESCRIPTOR(func, info) ((void *)func)

#endif
