/*
 * Copyright 2001-2009, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		DarkWyrm <bpmagic@columbus.rr.com>
 *		Stephan Aßmus <superstippi@gmx.de>
 *		Axel Dörfler, axeld@pinc-software.de
 */


/*!	Although descended from ServerBitmaps, ServerCursors are not handled by
	the BitmapManager, but the CursorManager instead. Until they have been
	attached to a CursorManager, you can delete cursors like any other object.

	Unlike BeOS, cursors can be any size or color space, and this class
	accommodates and expands the BeOS API.
*/


#include "CursorManager.h"
#include "ServerCursor.h"

#include <ByteOrder.h>

#include <new>
#include <stdio.h>


using std::nothrow;


/*!	\brief Constructor

	\param r Size of the cursor
	\param cspace Color space of the cursor
	\param flags ServerBitmap flags. See Bitmap.h.
	\param hotspot Hotspot of the cursor
	\param bytesperline Bytes per row for the cursor. See
		ServerBitmap::ServerBitmap()

*/
ServerCursor::ServerCursor(BRect r, color_space format, int32 flags,
		BPoint hotspot, int32 bytesPerRow, screen_id screen)
	:
	ServerBitmap(r, format, flags, bytesPerRow, screen),
	fHotSpot(hotspot),
	fOwningTeam(-1),
	fCursorData(NULL),
	fManager(NULL),
	fAnimationFrameCount(0),
	fAnimationFrameDelayMs(100),
	fAnimationStartDelayMs(0),
	fAnimationIdleDelayMs(0),
	fAnimationTrigger(B_CURSOR_ANI_TRIGGER_IMMEDIATE)
{
	fHotSpot.ConstrainTo(Bounds());
	AllocateBuffer();
}


/*!	\brief Constructor
	\param data Pointer to 68-byte cursor data array. See BeBook entry for
		BCursor for details
*/
ServerCursor::ServerCursor(const uint8* data)
	:
	ServerBitmap(BRect(0, 0, 15, 15), B_RGBA32, 0),
	fHotSpot(0, 0),
	fOwningTeam(-1),
	fCursorData(NULL),
	fManager(NULL),
	fAnimationFrameCount(0),
	fAnimationFrameDelayMs(100),
	fAnimationStartDelayMs(0),
	fAnimationIdleDelayMs(0),
	fAnimationTrigger(B_CURSOR_ANI_TRIGGER_IMMEDIATE)
{
	// 68-byte array used in BeOS for holding cursors.
	// This API has serious problems and should be deprecated (but supported)
	// in R2

	// Now that we have all the setup, we're going to map (for now) the cursor
	// to RGBA32 (little endian). Eventually, there will be support for 16 and
	// 8-bit depths
	// NOTE: review this once we have working PPC graphics cards (big endian).
	if (data) {
		AllocateBuffer();
		uint8* buffer = Bits();
		if (!buffer)
			return;

		uint16* cursorBits = (uint16*)(data + 4);
		uint16* transparencyBits = (uint16*)(data + 36);
		fHotSpot.Set(data[3], data[2]);

		// for each row in the cursor data
		for (int32 j = 0; j < 16; j++) {
			uint32* bits = (uint32*)(buffer + (j * BytesPerRow()));

			// On intel, our bytes end up swapped, so we must swap them back
			uint16 cursorLine = __swap_int16(cursorBits[j]);
			uint16 transparencyLine = __swap_int16(transparencyBits[j]);

			uint16 mask = 1 << 15;

			// for each column in each row of cursor data
			for (int32 i = 0; i < 16; i++, mask >>= 1) {
				// Get the values and dump them to the bitmap
				if (cursorLine & mask)
					bits[i] = 0xff000000; // black
				else if (transparencyLine & mask)
					bits[i] = 0xffffffff; // white
				else
					bits[i] = 0x00000000; // transparent
			}
		}

		// remember cursor data for later
		fCursorData = new (nothrow) uint8[68];
		if (fCursorData)
			memcpy(fCursorData, data, 68);

	} else {
		fWidth = 0;
		fHeight = 0;
		fBytesPerRow = 0;
		fSpace = B_NO_COLOR_SPACE;
	}
}


/*!	\brief Constructor
	\param data Pointer to bitmap data in memory,
	the padding bytes should be contained when format less than 32 bpp.
*/
ServerCursor::ServerCursor(const uint8* alreadyPaddedData, uint32 width,
		uint32 height, color_space format)
	:
	ServerBitmap(BRect(0, 0, width - 1, height - 1), format, 0),
	fHotSpot(0, 0),
	fOwningTeam(-1),
	fCursorData(NULL),
	fManager(NULL),
	fAnimationFrameCount(0),
	fAnimationFrameDelayMs(100),
	fAnimationStartDelayMs(0),
	fAnimationIdleDelayMs(0),
	fAnimationTrigger(B_CURSOR_ANI_TRIGGER_IMMEDIATE)
{
	AllocateBuffer();
	if (Bits())
		memcpy(Bits(), alreadyPaddedData, BitsLength());
}


/*!	\brief Copy constructor
	\param cursor cursor to copy
*/
ServerCursor::ServerCursor(const ServerCursor* cursor)
	:
	ServerBitmap(cursor),
	fHotSpot(0, 0),
	fOwningTeam(-1),
	fCursorData(NULL),
	fManager(NULL),
	fAnimationFrameCount(0),
	fAnimationFrameDelayMs(100),
	fAnimationStartDelayMs(0),
	fAnimationIdleDelayMs(0),
	fAnimationTrigger(B_CURSOR_ANI_TRIGGER_IMMEDIATE)
{
	// TODO: Hm. I don't move this into the if clause,
	// because it might break code elsewhere.
	AllocateBuffer();

	if (cursor) {
		if (Bits() && cursor->Bits())
			memcpy(Bits(), cursor->Bits(), BitsLength());
		fHotSpot = cursor->fHotSpot;
		if (cursor->fCursorData) {
			fCursorData = new (nothrow) uint8[68];
			if (fCursorData)
				memcpy(fCursorData, cursor->fCursorData, 68);
		}
		_CopyAnimationFrom(*cursor);
	}
}


//!	Frees the heap space allocated for the cursor's image data
ServerCursor::~ServerCursor()
{
	_ResetAnimationFrames();
	delete[] fCursorData;
}


/*!	\brief Sets the cursor's hotspot
	\param pt New location of hotspot, constrained to the cursor's boundaries.
*/
void
ServerCursor::SetHotSpot(BPoint hotSpot)
{
	fHotSpot = hotSpot;
	fHotSpot.ConstrainTo(Bounds());
}


void
ServerCursor::AttachedToManager(CursorManager* manager)
{
	fManager = manager;
}


status_t
ServerCursor::ConfigureAnimation(uint32 frameCount, uint32 frameDelayMs,
	uint32 startDelayMs, uint32 idleDelayMs, uint32 trigger)
{
	if (frameCount < 2 || frameCount > B_CURSOR_MAX_ANIMATION_FRAMES)
		return B_BAD_VALUE;
	if (trigger != B_CURSOR_ANI_TRIGGER_IMMEDIATE
		&& trigger != B_CURSOR_ANI_TRIGGER_IDLE) {
		return B_BAD_VALUE;
	}

	_ResetAnimationFrames();
	fAnimationFrameCount = frameCount;
	fAnimationFrameDelayMs = max_c((uint32)1, frameDelayMs);
	fAnimationStartDelayMs = startDelayMs;
	fAnimationIdleDelayMs = idleDelayMs;
	fAnimationTrigger = trigger;
	return B_OK;
}


status_t
ServerCursor::SetAnimationFrame(uint32 frameIndex, const uint8* data,
	uint32 bytesPerRow, color_space colorSpace, size_t dataSize)
{
	if (frameIndex < 1 || frameIndex >= fAnimationFrameCount || data == NULL)
		return B_BAD_VALUE;
	if (bytesPerRow == 0)
		return B_BAD_VALUE;
	if (colorSpace != ColorSpace())
		return B_BAD_VALUE;

	const uint16 height = (uint16)Bounds().IntegerHeight() + 1;
	const size_t minDataSize = (size_t)bytesPerRow * height;
	if (dataSize < minDataSize)
		return B_BAD_VALUE;

	animation_frame& frame = fAnimationFrames[frameIndex - 1];
	delete[] frame.bits;
	frame.bits = new (nothrow) uint8[minDataSize];
	if (frame.bits == NULL) {
		frame.bitsLength = 0;
		frame.bytesPerRow = 0;
		frame.colorSpace = B_NO_COLOR_SPACE;
		return B_NO_MEMORY;
	}

	memcpy(frame.bits, data, minDataSize);
	frame.bitsLength = minDataSize;
	frame.bytesPerRow = (uint16)bytesPerRow;
	frame.colorSpace = colorSpace;
	return B_OK;
}


bool
ServerCursor::GetAnimationFrame(uint32 frameIndex, const uint8*& bits,
	uint16& width, uint16& height, uint16& bytesPerRow,
	color_space& colorSpace) const
{
	if (frameIndex >= fAnimationFrameCount)
		return false;

	width = (uint16)Bounds().IntegerWidth() + 1;
	height = (uint16)Bounds().IntegerHeight() + 1;

	if (frameIndex == 0) {
		bits = Bits();
		bytesPerRow = (uint16)BytesPerRow();
		colorSpace = ColorSpace();
		return bits != NULL;
	}

	const animation_frame& frame = fAnimationFrames[frameIndex - 1];
	if (frame.bits == NULL || frame.bytesPerRow == 0)
		return false;

	bits = frame.bits;
	bytesPerRow = frame.bytesPerRow;
	colorSpace = frame.colorSpace;
	return true;
}


void
ServerCursor::ResetAnimation()
{
	_ResetAnimationFrames();
}


void
ServerCursor::_ResetAnimationFrames()
{
	for (uint32 i = 0; i < B_CURSOR_MAX_ANIMATION_FRAMES - 1; i++) {
		delete[] fAnimationFrames[i].bits;
		fAnimationFrames[i].bits = NULL;
		fAnimationFrames[i].bitsLength = 0;
		fAnimationFrames[i].bytesPerRow = 0;
		fAnimationFrames[i].colorSpace = B_NO_COLOR_SPACE;
	}

	fAnimationFrameCount = 0;
	fAnimationFrameDelayMs = 100;
	fAnimationStartDelayMs = 0;
	fAnimationIdleDelayMs = 0;
	fAnimationTrigger = B_CURSOR_ANI_TRIGGER_IMMEDIATE;
}


void
ServerCursor::_CopyAnimationFrom(const ServerCursor& other)
{
	if (!other.HasAnimation())
		return;

	if (ConfigureAnimation(other.fAnimationFrameCount,
			other.fAnimationFrameDelayMs, other.fAnimationStartDelayMs,
			other.fAnimationIdleDelayMs, other.fAnimationTrigger) != B_OK) {
		return;
	}

	for (uint32 i = 1; i < other.fAnimationFrameCount; i++) {
		const animation_frame& src = other.fAnimationFrames[i - 1];
		if (src.bits == NULL || src.bytesPerRow == 0) {
			_ResetAnimationFrames();
			return;
		}

		if (SetAnimationFrame(i, src.bits, src.bytesPerRow, src.colorSpace,
				src.bitsLength) != B_OK) {
			_ResetAnimationFrames();
			return;
		}
	}
}
