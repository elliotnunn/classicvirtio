/* Copyright (c) 2023 Elliot Nunn */
/* Licensed under the MIT license */

#include <ConditionalMacros.h>
#if GENERATINGCFM

#include <DriverSynchronization.h>
#include <MixedMode.h>
#include <Patches.h>
#include <Quickdraw.h>
#include <QuickdrawText.h>
#include <stdint.h>
#include <Traps.h>
#include <Types.h>

#include "dirtyrectpatch.h"

// X(trap, StdName, arguments, procInfo)
#define PATCH_LIST \
	X( \
		0xa882, \
		StdText, \
		(short count, const void *textAddr, Point numer, Point denom), \
		kPascalStackBased \
			| STACK_ROUTINE_PARAMETER(1, kTwoByteCode) \
			| STACK_ROUTINE_PARAMETER(2, kFourByteCode) \
			| STACK_ROUTINE_PARAMETER(3, kFourByteCode) \
			| STACK_ROUTINE_PARAMETER(4, kFourByteCode) \
	) \
	X( \
		0xa890, \
		StdLine, \
		(Point newPt), \
		kPascalStackBased \
			| STACK_ROUTINE_PARAMETER(1, kFourByteCode) \
	) \
	X( \
		0xa8a0, \
		StdRect, \
		(GrafVerb verb, const Rect *rect), \
		kPascalStackBased \
			| STACK_ROUTINE_PARAMETER(1, kOneByteCode) \
			| STACK_ROUTINE_PARAMETER(2, kFourByteCode) \
	) \
	X( \
		0xa8af, \
		StdRRect, \
		(GrafVerb verb, const Rect *rect, short ovalWidth, short ovalHeight), \
		kPascalStackBased \
			| STACK_ROUTINE_PARAMETER(1, kOneByteCode) \
			| STACK_ROUTINE_PARAMETER(2, kFourByteCode) \
			| STACK_ROUTINE_PARAMETER(3, kTwoByteCode) \
			| STACK_ROUTINE_PARAMETER(4, kTwoByteCode) \
	) \
	X( \
		0xa8b6, \
		StdOval, \
		(GrafVerb verb, const Rect *r), \
		kPascalStackBased \
			| STACK_ROUTINE_PARAMETER(1, kOneByteCode) \
			| STACK_ROUTINE_PARAMETER(2, kFourByteCode) \
	) \
	X( \
		0xa8bd, \
		StdArc, \
		(GrafVerb verb, const Rect *r, short startAngle, short arcAngle), \
		kPascalStackBased \
			| STACK_ROUTINE_PARAMETER(1, kOneByteCode) \
			| STACK_ROUTINE_PARAMETER(2, kFourByteCode) \
			| STACK_ROUTINE_PARAMETER(3, kTwoByteCode) \
			| STACK_ROUTINE_PARAMETER(4, kTwoByteCode) \
	) \
	X( \
		0xa8c5, \
		StdPoly, \
		(GrafVerb verb, PolyHandle poly), \
		kPascalStackBased \
			| STACK_ROUTINE_PARAMETER(1, kOneByteCode) \
			| STACK_ROUTINE_PARAMETER(2, kFourByteCode) \
	) \
	X( \
		0xa8d1, \
		StdRgn, \
		(GrafVerb verb, RgnHandle rgn), \
		kPascalStackBased \
			| STACK_ROUTINE_PARAMETER(1, kOneByteCode) \
			| STACK_ROUTINE_PARAMETER(2, kFourByteCode) \
	) \
	X( \
		0xa8eb, \
		StdBits, \
		(const BitMap *srcBits, const Rect *srcRect, const Rect *dstRect, short mode, RgnHandle maskRgn), \
		kPascalStackBased \
			| STACK_ROUTINE_PARAMETER(1, kFourByteCode) \
			| STACK_ROUTINE_PARAMETER(2, kFourByteCode) \
			| STACK_ROUTINE_PARAMETER(3, kFourByteCode) \
			| STACK_ROUTINE_PARAMETER(4, kTwoByteCode) \
			| STACK_ROUTINE_PARAMETER(5, kFourByteCode) \
	) \
	X( \
		0xa8ec, \
		CopyBits, \
		(const BitMap *srcBits, const BitMap *dstBits, const Rect *srcRect, const Rect *dstRect, short mode, RgnHandle maskRgn), \
		kPascalStackBased \
			| STACK_ROUTINE_PARAMETER(1, kFourByteCode) \
			| STACK_ROUTINE_PARAMETER(2, kFourByteCode) \
			| STACK_ROUTINE_PARAMETER(3, kFourByteCode) \
			| STACK_ROUTINE_PARAMETER(4, kFourByteCode) \
			| STACK_ROUTINE_PARAMETER(5, kTwoByteCode) \
			| STACK_ROUTINE_PARAMETER(6, kFourByteCode) \
	) \
	X( \
		0xa817, \
		CopyMask, \
		(const BitMap *srcBits, const BitMap *maskBits, const BitMap *dstBits, const Rect *srcRect, const Rect *maskRect, const Rect *dstRect), \
		kPascalStackBased \
			| STACK_ROUTINE_PARAMETER(1, kFourByteCode) \
			| STACK_ROUTINE_PARAMETER(2, kFourByteCode) \
			| STACK_ROUTINE_PARAMETER(3, kFourByteCode) \
			| STACK_ROUTINE_PARAMETER(4, kFourByteCode) \
			| STACK_ROUTINE_PARAMETER(5, kFourByteCode) \
			| STACK_ROUTINE_PARAMETER(6, kFourByteCode) \
	) \
	X( \
		0xaa51, \
		CopyDeepMask, \
		(const BitMap *srcBits, const BitMap *maskBits, const BitMap *dstBits, const Rect *srcRect, const Rect *maskRect, const Rect *dstRect, short mode, RgnHandle maskRgn), \
		kPascalStackBased \
			| STACK_ROUTINE_PARAMETER(1, kFourByteCode) \
			| STACK_ROUTINE_PARAMETER(2, kFourByteCode) \
			| STACK_ROUTINE_PARAMETER(3, kFourByteCode) \
			| STACK_ROUTINE_PARAMETER(4, kFourByteCode) \
			| STACK_ROUTINE_PARAMETER(5, kFourByteCode) \
			| STACK_ROUTINE_PARAMETER(6, kFourByteCode) \
			| STACK_ROUTINE_PARAMETER(7, kTwoByteCode) \
			| STACK_ROUTINE_PARAMETER(8, kFourByteCode) \
	) \

// The classics
#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

#define ISSCREEN(bitmapPtr) ( \
	*(void **)0x824 == ( \
		(((bitmapPtr)->rowBytes & 0xc000) == 0xc000) ? \
		(***(PixMap ***)(bitmapPtr)).baseAddr : \
		(bitmapPtr)->baseAddr \
	) \
)

#define CLIP(rectarg, tp, lt, bt, rt) { \
	const Rect *rect = rectarg; \
	tp = MAX(tp, rect->top); \
	lt = MAX(lt, rect->left); \
	bt = MIN(bt, rect->bottom); \
	rt = MIN(rt, rect->right); \
}

#define LOCALTOGLOBAL(bitmapPtr, tp, lt, bt, rt) { \
	const Rect *MACROBOUNDS; \
	if (((bitmapPtr)->rowBytes & 0xc000) == 0xc000) \
		MACROBOUNDS = &(***(PixMap ***)(bitmapPtr)).bounds; \
	else \
		MACROBOUNDS = &(bitmapPtr)->bounds; \
	tp -= MACROBOUNDS->top; \
	bt -= MACROBOUNDS->top; \
	lt -= MACROBOUNDS->left; \
	rt -= MACROBOUNDS->left; \
}

// Enum of MixedMode function signatures for the traps we patch
enum {
	#define X(trap, StdName, args, procInfo) k##StdName##ProcInfo = procInfo,
	PATCH_LIST
	#undef X
};

// Globals in which to store UPPs to old trap handlers
#define X(trap, StdName, args, procInfo) UniversalProcPtr their##StdName;
PATCH_LIST
#undef X

// Prototypes for our new trap handlers
#define X(trap, StdName, args, procInfo) static void my##StdName args;
PATCH_LIST
#undef X

// MixedMode outine descriptors for our new trap handlers
#define X(trap, StdName, args, procInfo) \
	static RoutineDescriptor my##StdName##Desc = BUILD_ROUTINE_DESCRIPTOR( \
		k##StdName##ProcInfo, my##StdName);
PATCH_LIST
#undef X

void InstallDirtyRectPatch(void) {
	// Install our patches, saving the old traps
	#define X(trap, StdName, args, procInfo) \
		if (trap >= 0xa800) { \
			their##StdName = GetToolTrapAddress(trap); \
			SetToolTrapAddress(&my##StdName##Desc, trap); \
		} else if (trap >= 0xa000) { \
			their##StdName = GetOSTrapAddress(trap); \
			SetOSTrapAddress(&my##StdName##Desc, trap); \
		} else { \
			their##StdName = *(void **)trap; \
			*(void **)trap = &my##StdName##Desc; \
		}
	PATCH_LIST
	#undef X
}

static void myStdText(short count, const void *textAddr, Point numer, Point denom) {
	int t, l, b, r;

	GrafPort *port;
	GetPort(&port);

	l = port->pnLoc.h - 1;
	CallUniversalProc(theirStdText, kStdTextProcInfo, count, textAddr, numer, denom);
	r = port->pnLoc.h + 1;

	if (port->picSave || !ISSCREEN(&port->portBits)) return;

	if (numer.v == 1 && denom.v == 1 && port->txSize != 0) {
		// Avoid calling StdTxMeas in the simple case,
		// at the expense of overestimating the rect.
		t = port->pnLoc.v - port->txSize;
		b = port->pnLoc.v + port->txSize;
	} else {
		FontInfo info;
		StdTxMeas(count, textAddr, &numer, &denom, &info);
		t = port->pnLoc.v - info.ascent - 1;
		b = port->pnLoc.v + info.descent + 1;
	}

	CLIP(&(*port->clipRgn)->rgnBBox, t, l, b, r);
	CLIP(&(*port->visRgn)->rgnBBox, t, l, b, r);
	LOCALTOGLOBAL(&port->portBits, t, l, b, r);
	DirtyRectCallback(t, l, b, r);
}

static void myStdLine(Point newPt) {
	int t, l, b, r;

	GrafPort *port;
	GetPort(&port);

	t = port->pnLoc.v;
	l = port->pnLoc.h;
	b = newPt.v;
	r = newPt.h;

	CallUniversalProc(theirStdLine, kStdLineProcInfo, newPt);
	if (port->picSave || !ISSCREEN(&port->portBits)) return;

	if (t > b) {
		int swap = t;
		t = b;
		b = swap;
	}

	if (l > r) {
		int swap = l;
		l = r;
		r = swap;
	}

	b += port->pnSize.v;
	r += port->pnSize.h;

	CLIP(&(*port->clipRgn)->rgnBBox, t, l, b, r);
	CLIP(&(*port->visRgn)->rgnBBox, t, l, b, r);
	LOCALTOGLOBAL(&port->portBits, t, l, b, r);
	DirtyRectCallback(t, l, b, r);
}

static void myStdRect(GrafVerb verb, const Rect *rect) {
	int t, l, b, r;
	GrafPort *port;

	CallUniversalProc(theirStdRect, kStdRectProcInfo, verb, rect);

	GetPort(&port);
	if (port->picSave || !ISSCREEN(&port->portBits)) return;

	t = rect->top;
	l = rect->left;
	b = rect->bottom;
	r = rect->right;

	CLIP(&(*port->clipRgn)->rgnBBox, t, l, b, r);
	CLIP(&(*port->visRgn)->rgnBBox, t, l, b, r);
	LOCALTOGLOBAL(&port->portBits, t, l, b, r);
	DirtyRectCallback(t, l, b, r);
}

static void myStdRRect(GrafVerb verb, const Rect *rect, short ovalWidth, short ovalHeight) {
	int t, l, b, r;
	GrafPort *port;

	CallUniversalProc(theirStdRRect, kStdRRectProcInfo, verb, rect, ovalWidth, ovalHeight);

	GetPort(&port);
	if (port->picSave || !ISSCREEN(&port->portBits)) return;

	t = rect->top;
	l = rect->left;
	b = rect->bottom;
	r = rect->right;

	CLIP(&(*port->clipRgn)->rgnBBox, t, l, b, r);
	CLIP(&(*port->visRgn)->rgnBBox, t, l, b, r);
	LOCALTOGLOBAL(&port->portBits, t, l, b, r);
	DirtyRectCallback(t, l, b, r);
}

static void myStdOval(GrafVerb verb, const Rect *rect) {
	int t, l, b, r;
	GrafPort *port;

	CallUniversalProc(theirStdOval, kStdOvalProcInfo, verb, rect);

	GetPort(&port);
	if (port->picSave || !ISSCREEN(&port->portBits)) return;

	t = rect->top;
	l = rect->left;
	b = rect->bottom;
	r = rect->right;

	CLIP(&(*port->clipRgn)->rgnBBox, t, l, b, r);
	CLIP(&(*port->visRgn)->rgnBBox, t, l, b, r);
	LOCALTOGLOBAL(&port->portBits, t, l, b, r);
	DirtyRectCallback(t, l, b, r);
}

static void myStdArc(GrafVerb verb, const Rect *rect, short startAngle, short arcAngle) {
	int t, l, b, r;
	GrafPort *port;

	CallUniversalProc(theirStdArc, kStdArcProcInfo, verb, rect, startAngle, arcAngle);

	GetPort(&port);
	if (port->picSave || !ISSCREEN(&port->portBits)) return;

	t = rect->top;
	l = rect->left;
	b = rect->bottom;
	r = rect->right;

	CLIP(&(*port->clipRgn)->rgnBBox, t, l, b, r);
	CLIP(&(*port->visRgn)->rgnBBox, t, l, b, r);
	LOCALTOGLOBAL(&port->portBits, t, l, b, r);
	DirtyRectCallback(t, l, b, r);
}

static void myStdPoly(GrafVerb verb, PolyHandle poly) {
	Rect *rect;
	int t, l, b, r;
	GrafPort *port;

	CallUniversalProc(theirStdPoly, kStdPolyProcInfo, verb, poly);

	GetPort(&port);
	if (port->picSave || !ISSCREEN(&port->portBits)) return;

	rect = &(**poly).polyBBox;
	t = rect->top;
	l = rect->left;
	b = rect->bottom + port->pnSize.v;
	r = rect->right + port->pnSize.h;

	CLIP(&(*port->clipRgn)->rgnBBox, t, l, b, r);
	CLIP(&(*port->visRgn)->rgnBBox, t, l, b, r);
	LOCALTOGLOBAL(&port->portBits, t, l, b, r);
	DirtyRectCallback(t, l, b, r);
}

static void myStdRgn(GrafVerb verb, RgnHandle rgn) {
	Rect *rect;
	int t, l, b, r;
	GrafPort *port;

	CallUniversalProc(theirStdRgn, kStdRgnProcInfo, verb, rgn);

	GetPort(&port);
	if (port->picSave || !ISSCREEN(&port->portBits)) return;

	rect = &(**rgn).rgnBBox;
	t = rect->top;
	l = rect->left;
	b = rect->bottom;
	r = rect->right;

	CLIP(&(*port->clipRgn)->rgnBBox, t, l, b, r);
	CLIP(&(*port->visRgn)->rgnBBox, t, l, b, r);
	LOCALTOGLOBAL(&port->portBits, t, l, b, r);
	DirtyRectCallback(t, l, b, r);
}

static void myStdBits(const BitMap *srcBits, const Rect *srcRect, const Rect *dstRect, short mode, RgnHandle maskRgn) {
	int t, l, b, r;
	GrafPort *port;

	CallUniversalProc(theirStdBits, kStdBitsProcInfo, srcBits, srcRect, dstRect, mode, maskRgn);

	GetPort(&port);
	if (port->picSave || !ISSCREEN(&port->portBits)) return;

	t = dstRect->top;
	l = dstRect->left;
	b = dstRect->bottom;
	r = dstRect->right;

	CLIP(&(*port->clipRgn)->rgnBBox, t, l, b, r);
	CLIP(&(*port->visRgn)->rgnBBox, t, l, b, r);
	LOCALTOGLOBAL(&port->portBits, t, l, b, r);
	DirtyRectCallback(t, l, b, r);
}

static void myCopyBits(const BitMap *srcBits, const BitMap *dstBits, const Rect *srcRect, const Rect *dstRect, short mode, RgnHandle maskRgn) {
	int t, l, b, r;
	GrafPort *port;

	CallUniversalProc(theirCopyBits, kCopyBitsProcInfo, srcBits, dstBits, srcRect, dstRect, mode, maskRgn);

	if (!ISSCREEN(dstBits)) return;

	t = dstRect->top;
	l = dstRect->left;
	b = dstRect->bottom;
	r = dstRect->right;

	GetPort(&port);
	if (dstBits == &port->portBits) {
		CLIP(&(*port->clipRgn)->rgnBBox, t, l, b, r);
		CLIP(&(*port->visRgn)->rgnBBox, t, l, b, r);
	}

	LOCALTOGLOBAL(dstBits, t, l, b, r);
	DirtyRectCallback(t, l, b, r);
}

static void myCopyMask(const BitMap *srcBits, const BitMap *maskBits, const BitMap *dstBits, const Rect *srcRect, const Rect *maskRect, const Rect *dstRect) {
	int t, l, b, r;
	GrafPort *port;

	CallUniversalProc(theirCopyMask, kCopyMaskProcInfo, srcBits, maskBits, dstBits, srcRect, maskRect, dstRect);

	if (!ISSCREEN(dstBits)) return;

	t = dstRect->top;
	l = dstRect->left;
	b = dstRect->bottom;
	r = dstRect->right;

	GetPort(&port);
	if (dstBits == &port->portBits) {
		CLIP(&(*port->clipRgn)->rgnBBox, t, l, b, r);
		CLIP(&(*port->visRgn)->rgnBBox, t, l, b, r);
	}

	LOCALTOGLOBAL(dstBits, t, l, b, r);
	DirtyRectCallback(t, l, b, r);
}

static void myCopyDeepMask(const BitMap *srcBits, const BitMap *maskBits, const BitMap *dstBits, const Rect *srcRect, const Rect *maskRect, const Rect *dstRect, short mode, RgnHandle maskRgn) {
	int t, l, b, r;
	GrafPort *port;

	CallUniversalProc(theirCopyDeepMask, kCopyDeepMaskProcInfo, srcBits, maskBits, dstBits, srcRect, maskRect, dstRect, mode, maskRgn);

	if (!ISSCREEN(dstBits)) return;

	t = dstRect->top;
	l = dstRect->left;
	b = dstRect->bottom;
	r = dstRect->right;

	GetPort(&port);
	if (dstBits == &port->portBits) {
		CLIP(&(*port->clipRgn)->rgnBBox, t, l, b, r);
		CLIP(&(*port->visRgn)->rgnBBox, t, l, b, r);
	}

	LOCALTOGLOBAL(dstBits, t, l, b, r);
	DirtyRectCallback(t, l, b, r);
}

#endif
