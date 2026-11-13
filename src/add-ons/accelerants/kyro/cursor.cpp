/*
 * Copyright 2026, Fabio Tomat.
 * Copyright 2026, GitHub Copilot.
 *
 * Distributed under the terms of the GNU General Public License version 2.
 */


#include "accelerant.h"

#include <math.h>
#include <OS.h>
#include <string.h>


namespace {

static const uint32 kDACCursorAddr = 0x1408;
static const uint32 kDACCursorCtrl = 0x140c;

// Found by trial and error on real hardware (the STG4000 cursor buffer
// layout isn't documented anywhere we have access to): a raw 8bpp
// grayscale canvas, 32 pixels wide and 32 rows tall - 1024 bytes total,
// matching the fixed cursor buffer size - produced the cursor glyph at
// exactly the right size and proportions.
static const uint32 kCursorWidth = 32;
static const uint32 kCursorHeight = 32;

struct cursor_animation_state {
	bool		running;
	bool		terminate;
	float		speedFactor;
	bigtime_t	frameInterval;
	uint32		uploadedFrameCount;
	uint32		nextUploadFrame;
	uint32		currentFrame;
	sem_id		semaphore;
	thread_id	thread;
};

static cursor_animation_state sCursorAnimation = {
	false,
	false,
	10.0f,
	100000,
	1,
	0,
	0,
	-1,
	-1
};


static inline uint32
read32(uint32 offset)
{
	return *(volatile uint32*)(gInfo.regs + offset);
}


static inline void
write32(uint32 offset, uint32 value)
{
	*(volatile uint32*)(gInfo.regs + offset) = value;
}


static uint8*
cursor_buffer(uint32 frameIndex)
{
	if (gInfo.frameBuffer == NULL || gInfo.sharedInfo == NULL)
		return NULL;
	if (frameIndex >= gInfo.sharedInfo->cursorFrameBufferCount)
		return NULL;
	return gInfo.frameBuffer + gInfo.sharedInfo->cursorFrameOffsets[frameIndex];
}


// At the right size, writing a single grayscale luma byte per pixel (0 =
// black, 255 = white) produced the cursor glyph in a pink/flesh tint that
// shifted with the glyph's shape - the tell-tale sign of a smoothly
// varying scalar being decoded as a packed color rather than a brightness
// level (white and black still landed on 0xff/0x00 either way, but every
// in-between value - the antialiased edges - came out as some other hue
// instead of gray). So instead of luma, pack red/green/blue directly into
// the byte, trading off precision per channel like a packed 16-bit color
// would: RGB332 (3 bits red, 3 bits green, 2 bits blue). White and black
// still land on 0xff/0x00 as before, but now the byte actually encodes
// that gray relationship instead of an arbitrary brightness number.
static void
cursor_set_pixel(uint8* dest, uint32 x, uint32 y, uint8 red, uint8 green,
	uint8 blue, uint8 alpha)
{
	uint8 value = (red & 0xe0) | ((green & 0xe0) >> 3) | (blue >> 6);
	dest[y * kCursorWidth + x] = alpha < 128 ? 0x04 : value;
}


static void
program_cursor_address(uint32 frameIndex)
{
	if (gInfo.sharedInfo == NULL
		|| frameIndex >= gInfo.sharedInfo->cursorFrameBufferCount) {
		return;
	}

	uint32 value = read32(kDACCursorAddr);
	value &= ~0x001fffff;
	value |= (gInfo.sharedInfo->cursorFrameOffsets[frameIndex] >> 4) & 0x001fffff;
	write32(kDACCursorAddr, value);
}


static status_t
cursor_animation_thread(void* cookie)
{
	(void)cookie;

	while (!sCursorAnimation.terminate) {
		bigtime_t timeout = sCursorAnimation.running
			? sCursorAnimation.frameInterval : 500000;
		status_t status = acquire_sem_etc(sCursorAnimation.semaphore, 1,
			B_RELATIVE_TIMEOUT | B_CAN_INTERRUPT, timeout);
		if (sCursorAnimation.terminate)
			break;

		if (!sCursorAnimation.running || sCursorAnimation.uploadedFrameCount < 2) {
			if (status == B_INTERRUPTED)
				continue;
			continue;
		}

		if (status != B_TIMED_OUT && status != B_OK && status != B_INTERRUPTED)
			continue;

		sCursorAnimation.currentFrame++;
		if (sCursorAnimation.currentFrame >= sCursorAnimation.uploadedFrameCount)
			sCursorAnimation.currentFrame = 0;

		program_cursor_address(sCursorAnimation.currentFrame);
	}

	return B_OK;
}


static status_t
ensure_animation_thread(void)
{
	if (sCursorAnimation.thread >= B_OK)
		return B_OK;

	sCursorAnimation.semaphore = create_sem(0, "kyro cursor ani");
	if (sCursorAnimation.semaphore < B_OK)
		return sCursorAnimation.semaphore;

	sCursorAnimation.terminate = false;
	sCursorAnimation.thread = spawn_thread(cursor_animation_thread,
		"kyro cursor ani", B_DISPLAY_PRIORITY, NULL);
	if (sCursorAnimation.thread < B_OK) {
		delete_sem(sCursorAnimation.semaphore);
		sCursorAnimation.semaphore = -1;
		return sCursorAnimation.thread;
	}

	resume_thread(sCursorAnimation.thread);
	return B_OK;
}

} // namespace


uint32
GetCursorBits(void)
{
	return gInfo.sharedInfo->settings.cursorbits;
}


status_t
SetCursorShape(uint16 width, uint16 height, uint16 hotX, uint16 hotY,
	uint8* andMask, uint8* xorMask)
{
	if (width > 64 || height > 64)
		return B_BAD_VALUE;

	uint8* dest = cursor_buffer(0);
	if (dest == NULL)
		return B_NO_INIT;

	memset(dest, 0, kCursorWidth * kCursorHeight);

	const uint32 srcStride = (width + 7) / 8;
	const uint32 clipWidth = min_c(width, kCursorWidth);
	const uint32 clipHeight = min_c(height, kCursorHeight);

	for (uint32 y = 0; y < clipHeight; y++) {
		for (uint32 x = 0; x < clipWidth; x++) {
			uint32 srcByte = y * srcStride + x / 8;
			uint8 srcBit = 7 - (x % 8);
			bool andBit = ((andMask[srcByte] >> srcBit) & 1) != 0;
			bool xorBit = ((xorMask[srcByte] >> srcBit) & 1) != 0;

			// Matches the classic AND/XOR truth table: (0,0)->white,
			// (0,1)->black, (1,0)->transparent, (1,1)->invert
			// (approximated as opaque gray since a raw grayscale canvas
			// can't XOR the screen).
			uint8 red = 0, green = 0, blue = 0, alpha = 255;
			if (andBit && !xorBit)
				alpha = 0;
			else if (!andBit && !xorBit)
				red = green = blue = 255;
			else if (andBit && xorBit)
				red = green = blue = 128;
			cursor_set_pixel(dest, x, y, red, green, blue, alpha);
		}
	}

	gInfo.sharedInfo->cursor.hotX = hotX;
	gInfo.sharedInfo->cursor.hotY = hotY;
	program_cursor_address(0);
	MoveCursor(gInfo.sharedInfo->cursor.x, gInfo.sharedInfo->cursor.y);
	return B_OK;
}


status_t
SetCursorBitmap(uint16 width, uint16 height, uint16 hotX, uint16 hotY,
	color_space colorSpace, uint16 bytesPerRow, const uint8* bitmapData)
{
	if (width > 64 || height > 64)
		return B_BAD_VALUE;
	if (colorSpace != B_RGBA32 && colorSpace != B_RGB32)
		return B_BAD_VALUE;

	if (ensure_animation_thread() != B_OK)
		return B_ERROR;

	if (!sCursorAnimation.running) {
		if (sCursorAnimation.nextUploadFrame == 0)
			sCursorAnimation.uploadedFrameCount = 0;
		sCursorAnimation.currentFrame = sCursorAnimation.nextUploadFrame;
	}

	uint32 targetFrame = sCursorAnimation.running
		? sCursorAnimation.currentFrame : sCursorAnimation.nextUploadFrame;
	if (targetFrame >= gInfo.sharedInfo->cursorFrameBufferCount)
		targetFrame = 0;

	uint8* dest = cursor_buffer(targetFrame);
	if (dest == NULL)
		return B_NO_INIT;

	memset(dest, 0x04, kCursorWidth * kCursorHeight);
	// --- TEST DIAGNOSTICO: Riempiamo la griglia 32x32 con i 256 valori (0x00 - 0xFF) ---
    // Ripetiamo il pattern 4 volte per coprire l'intera matrice 32x32 (16x16 pixel per blocco di 256 valori)
    /*
    for (uint32 y = 0; y < kCursorHeight; y++) {
        for (uint32 x = 0; x < kCursorWidth; x++) {
            // Combiniamo X e Y per ottenere un valore progressivo da 0 a 255
            // Esempio: un gradiente/scacchiera che mappa ogni singolo byte
            uint8 testValue = (y * 8 + x / 4) & 0xFF; 
            
            // Oppure, se preferisci una mappatura lineare pura (i primi 256 pixel coprono tutti i valori):
            // uint32 index = y * kCursorWidth + x;
            // uint8 testValue = (index < 256) ? (uint8)index : 0;

            dest[y * kCursorWidth + x] = testValue;
        }
    }*/

	const uint32 clipWidth = min_c(width, kCursorWidth);
	const uint32 clipHeight = min_c(height, kCursorHeight);

	for (uint32 y = 0; y < clipHeight; y++) {
		for (uint32 x = 0; x < clipWidth; x++) {
			const uint8* pixel = bitmapData + y * bytesPerRow + x * 4;
			uint8 blue = pixel[0];
			uint8 green = pixel[1];
			uint8 red = pixel[2];
			uint8 alpha = colorSpace == B_RGBA32 ? pixel[3] : 255;
			cursor_set_pixel(dest, x, y, red, green, blue, alpha);
		}
	}

	gInfo.sharedInfo->cursor.hotX = hotX;
	gInfo.sharedInfo->cursor.hotY = hotY;
	program_cursor_address(targetFrame);
	MoveCursor(gInfo.sharedInfo->cursor.x, gInfo.sharedInfo->cursor.y);

	if (!sCursorAnimation.running) {
		if (targetFrame + 1 < gInfo.sharedInfo->cursorFrameBufferCount)
			sCursorAnimation.nextUploadFrame = targetFrame + 1;
		else
			sCursorAnimation.nextUploadFrame = 0;

		if (sCursorAnimation.uploadedFrameCount < targetFrame + 1)
			sCursorAnimation.uploadedFrameCount = targetFrame + 1;
	}

	return B_OK;
}


void
MoveCursor(uint16 x, uint16 y)
{
	gInfo.sharedInfo->cursor.x = x;
	gInfo.sharedInfo->cursor.y = y;

	int32 cursorX = (int32)x - gInfo.sharedInfo->cursor.hotX;
	int32 cursorY = (int32)y - gInfo.sharedInfo->cursor.hotY;
	if (cursorX < 0)
		cursorX = 0;
	if (cursorY < 0)
		cursorY = 0;

	// The DAC's cursor position register has its X/Y fields swapped with
	// respect to what the naming in the datasheet-derived macros suggests:
	// on real hardware the low field (bits 0-10) controls the vertical
	// position and the high field (bits 16-26) controls the horizontal
	// position. Confirmed by on-hardware testing: without this swap,
	// horizontal mouse movement moved the cursor vertically and vice versa.
	uint32 value = read32(kDACCursorCtrl);
	value &= ~((0x7ffu << 0) | (0x7ffu << 16));
	value |= (cursorY & 0x7ffu);
	value |= (cursorX & 0x7ffu) << 16;
	write32(kDACCursorCtrl, value);
}


void
ShowCursor(bool show)
{
	uint32 value = read32(kDACCursorCtrl);
	if (show)
		value |= 1u << 31;
	else
		value &= ~(1u << 31);
	write32(kDACCursorCtrl, value);
	gInfo.sharedInfo->cursor.visible = show;
}


bool
SupportsCursorAnimation(void)
{
	return gInfo.sharedInfo != NULL
		&& gInfo.sharedInfo->settings.hardcursor
		&& gInfo.sharedInfo->settings.cursorani;
}


uint32
CursorSupportedFrames(void)
{
	if (!SupportsCursorAnimation())
		return 0;

	if (gInfo.sharedInfo->cursorFrameBufferCount < 2)
		return 0;

	return gInfo.sharedInfo->cursorFrameBufferCount - 1;
}


status_t
StartCursorAnimation(void)
{
	if (!SupportsCursorAnimation())
		return B_NOT_SUPPORTED;

	if (sCursorAnimation.uploadedFrameCount < 2)
		return B_BAD_VALUE;

	if (ensure_animation_thread() != B_OK)
		return B_ERROR;

	sCursorAnimation.currentFrame = 0;
	program_cursor_address(0);
	MoveCursor(gInfo.sharedInfo->cursor.x, gInfo.sharedInfo->cursor.y);

	sCursorAnimation.running = true;
	if (sCursorAnimation.semaphore >= B_OK)
		release_sem(sCursorAnimation.semaphore);
	return B_OK;
}


status_t
StopCursorAnimation(void)
{
	if (!SupportsCursorAnimation())
		return B_NOT_SUPPORTED;

	sCursorAnimation.running = false;
	sCursorAnimation.currentFrame = 0;
	sCursorAnimation.nextUploadFrame = 0;
	program_cursor_address(0);
	MoveCursor(gInfo.sharedInfo->cursor.x, gInfo.sharedInfo->cursor.y);
	return B_OK;
}


status_t
SetCursorAnimationSpeed(float speedFactor)
{
	if (!SupportsCursorAnimation())
		return B_NOT_SUPPORTED;

	if (!isfinite(speedFactor) || speedFactor <= 0.0f)
		return B_BAD_VALUE;

	sCursorAnimation.speedFactor = speedFactor;
	sCursorAnimation.frameInterval = (bigtime_t)(1000000.0f / speedFactor);
	if (sCursorAnimation.frameInterval < 5000)
		sCursorAnimation.frameInterval = 5000;
	return B_OK;
}


void
CursorAnimationUninit(void)
{
	if (sCursorAnimation.thread >= B_OK) {
		sCursorAnimation.terminate = true;
		if (sCursorAnimation.semaphore >= B_OK)
			release_sem(sCursorAnimation.semaphore);
		status_t result;
		wait_for_thread(sCursorAnimation.thread, &result);
	}

	if (sCursorAnimation.semaphore >= B_OK)
		delete_sem(sCursorAnimation.semaphore);

	sCursorAnimation.running = false;
	sCursorAnimation.terminate = false;
	sCursorAnimation.speedFactor = 10.0f;
	sCursorAnimation.frameInterval = 100000;
	sCursorAnimation.uploadedFrameCount = 1;
	sCursorAnimation.nextUploadFrame = 0;
	sCursorAnimation.currentFrame = 0;
	sCursorAnimation.semaphore = -1;
	sCursorAnimation.thread = -1;
}
