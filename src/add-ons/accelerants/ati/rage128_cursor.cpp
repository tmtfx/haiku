/*
	Haiku ATI video driver adapted from the X.org ATI driver.

	Copyright 1999, 2000 ATI Technologies Inc., Markham, Ontario,
						 Precision Insight, Inc., Cedar Park, Texas, and
						 VA Linux Systems Inc., Fremont, California.

	Copyright 2009 Haiku, Inc.  All rights reserved.
	Distributed under the terms of the MIT license.

	Authors:
	Gerald Zajac 2009
*/


#include "accelerant.h"
#include "rage128.h"
#include <cstring>

#include <math.h>
#include <OS.h>


namespace {

// Animazione cursore hardware: stesso schema usato per kyro/radeon_hd. Un
// thread dedicato ricicla le slot caricate via Rage128_SetCursorBitmap()
// riprogrammando solo il registro di indirizzo superficie (tramite
// Rage128_SetCursorPosition, che gia' gestisce il clipping ai bordi).
struct cursor_animation_state {
	bool		running;
	bool		terminate;
	float		speedFactor;
	bigtime_t	frameInterval;
	bigtime_t	startDelay;
	uint32		uploadedFrameCount;
	uint32		nextUploadFrame;
	uint32		currentFrame;
	int			lastX;
	int			lastY;
	sem_id		semaphore;
	thread_id	thread;
};

cursor_animation_state sCursorAnimation = {
	false, false, 10.0f, 100000, 0, 1, 0, 0, 0, 0, -1, -1
};

} // namespace


static status_t
rage128_cursor_animation_thread(void*)
{
	while (!sCursorAnimation.terminate) {
		bigtime_t interval = sCursorAnimation.frameInterval;
		if (sCursorAnimation.currentFrame == 0 && sCursorAnimation.startDelay > 0)
			interval += sCursorAnimation.startDelay;

		bigtime_t timeout = sCursorAnimation.running ? interval : 500000;
		status_t status = acquire_sem_etc(sCursorAnimation.semaphore, 1,
			B_RELATIVE_TIMEOUT | B_CAN_INTERRUPT, timeout);
		if (sCursorAnimation.terminate)
			break;

		if (!sCursorAnimation.running || sCursorAnimation.uploadedFrameCount < 2)
			continue;
		if (status != B_TIMED_OUT)
			continue;

		sCursorAnimation.currentFrame++;
		if (sCursorAnimation.currentFrame >= sCursorAnimation.uploadedFrameCount)
			sCursorAnimation.currentFrame = 0;

		SharedInfo& si = *gInfo.sharedInfo;
		si.cursorActiveOffset = si.cursorFrameOffsets[sCursorAnimation.currentFrame];
		Rage128_SetCursorPosition(sCursorAnimation.lastX, sCursorAnimation.lastY);
	}

	return B_OK;
}


static status_t
rage128_ensure_animation_thread(void)
{
	if (sCursorAnimation.thread >= B_OK)
		return B_OK;

	sCursorAnimation.semaphore = create_sem(0, "rage128 cursor ani");
	if (sCursorAnimation.semaphore < B_OK)
		return sCursorAnimation.semaphore;

	sCursorAnimation.terminate = false;
	sCursorAnimation.thread = spawn_thread(rage128_cursor_animation_thread,
		"rage128 cursor ani", B_DISPLAY_PRIORITY, NULL);
	if (sCursorAnimation.thread < B_OK) {
		delete_sem(sCursorAnimation.semaphore);
		sCursorAnimation.semaphore = -1;
		return sCursorAnimation.thread;
	}

	resume_thread(sCursorAnimation.thread);
	return B_OK;
}


void
Rage128_ShowCursor(bool bShow)
{
	// Turn cursor on/off.

   	OUTREGM(R128_CRTC_GEN_CNTL, bShow ? R128_CRTC_CUR_EN : 0, R128_CRTC_CUR_EN);
}


void
Rage128_SetCursorPosition(int x, int y)
{
	SharedInfo& si = *gInfo.sharedInfo;

	sCursorAnimation.lastX = x;
	sCursorAnimation.lastY = y;

	// xOffset & yOffset are used for displaying partial cursors on screen edges.

	uint8 xOffset = 0;
	uint8 yOffset = 0;

	if (x < 0) {
		xOffset = (( -x) & 0xFE);
		x = 0;
	}

	if (y < 0) {
		yOffset = (( -y) & 0xFE);
		y = 0;
	}

	OUTREG(R128_CUR_HORZ_VERT_OFF,  R128_CUR_LOCK | (xOffset << 16) | yOffset);
	OUTREG(R128_CUR_HORZ_VERT_POSN, R128_CUR_LOCK | (x << 16) | y);
	OUTREG(R128_CUR_OFFSET, si.cursorActiveOffset + yOffset * 16);
}


bool
Rage128_LoadCursorImage(int width, int height, uint8* andMask, uint8* xorMask)
{
	SharedInfo& si = *gInfo.sharedInfo;

	if (andMask == NULL || xorMask == NULL)
		return false;

	// Initialize the hardware cursor as completely transparent.

	uint32* fbCursor32 = (uint32*)((addr_t)si.videoMemAddr + si.cursorOffset);

	for (int i = 0; i < CURSOR_BYTES; i += 16) {
		*fbCursor32++ = ~0;		// and bits
		*fbCursor32++ = ~0;
		*fbCursor32++ = 0;		// xor bits
		*fbCursor32++ = 0;
	}

	// Now load the AND & XOR masks for the cursor image into the cursor
	// buffer.  Note that a particular bit in these masks will have the
	// following effect upon the corresponding cursor pixel:
	//	AND  XOR	Result
	//	 0	0		 White pixel
	//	 0	1		 Black pixel
	//	 1	0		 Screen color (for transparency)
	//	 1	1		 Reverse screen color to black or white

	uint8* fbCursor = (uint8*)((addr_t)si.videoMemAddr + si.cursorOffset);

	for (int row = 0; row < height; row++) {
		for (int colByte = 0; colByte < width / 8; colByte++) {
			fbCursor[row * 16 + colByte] = *andMask++;
			fbCursor[row * 16 + colByte + 8] = *xorMask++;
		}
	}

	// Set the cursor colors which are white background and black foreground.

	OUTREG(R128_CUR_CLR0, ~0);
	OUTREG(R128_CUR_CLR1, 0);

	return true;
}

status_t
Rage128_SetCursorBitmap(uint16 width, uint16 height, uint16 hot_x, uint16 hot_y,
						 color_space colorSpace, uint16 bytesPerRow, const uint8* bitmapData)
{
	debug_printf("ATI_acc: rage128_cursor, setting bitmap\n");
	SharedInfo& si = *gInfo.sharedInfo;

	if (rage128_ensure_animation_thread() != B_OK)
		return B_ERROR;

	if (!sCursorAnimation.running) {
		if (sCursorAnimation.nextUploadFrame == 0)
			sCursorAnimation.uploadedFrameCount = 0;
		sCursorAnimation.currentFrame = sCursorAnimation.nextUploadFrame;
	}

	uint32 targetFrame = sCursorAnimation.running
		? sCursorAnimation.currentFrame : sCursorAnimation.nextUploadFrame;
	if (targetFrame >= si.cursorFrameCount)
		targetFrame = 0;

	uint8* fbCursor = (uint8*)((addr_t)si.videoMemAddr + si.cursorFrameOffsets[targetFrame]);
	if (fbCursor == NULL)
		return B_NO_INIT;

	// PLANAR INITIALIZATION PLANARE (512 bytes AND, 512 bytes XOR)
	// transparent: AND = 0xFF (all 1), XOR = 0x00 (all 0)
	memset(fbCursor, 0xFF, 512);	   // AND Plane
	memset(fbCursor + 512, 0x00, 512); // XOR Plane

	// COPY THE PIXELS ON TWO SEPARATED PLANES
	if (colorSpace == B_RGBA32 || colorSpace == B_RGB32) {
		for (int y = 0; y < height && y < 64; y++) {
			const uint8* srcRow = bitmapData + y * bytesPerRow;
			
			uint8* andRow = fbCursor + (y * 8);
			uint8* xorRow = fbCursor + 512 + (y * 8);

			for (int x = 0; x < width && x < 64; x++) {
				const uint8* pixel = srcRow + x * 4;
				
				uint8 b = pixel[0];
				uint8 g = pixel[1];
				uint8 r = pixel[2];
				uint8 a = (colorSpace == B_RGBA32) ? pixel[3] : 0xFF;

				if (a < 128)
					continue; // Keep transparent (AND=1, XOR=0)

				int byteIdx = x / 8;
				int bitShift = 7 - (x % 8);

				// Solid pixel
				andRow[byteIdx] &= ~(1 << bitShift);

				// Luma
				uint32 luma = (r * 77 + g * 150 + b * 29) >> 8;
				if (luma > 128) {
					// white -> AND=0, XOR=0 (XOR is already at 0 from initialization)
					xorRow[byteIdx] &= ~(1 << bitShift);
				} else {
					// black -> AND=0, XOR=1
					xorRow[byteIdx] |= (1 << bitShift);
				}
			}
		}
	} else {
		return B_ERROR;
	}

	// UPDATE CURSOR COLOR REGISTERS (Rage 128)
	OUTREG(R128_CUR_CLR0, 0xFFFFFF); // Background White
	OUTREG(R128_CUR_CLR1, 0x000000); // Foreground Black

	si.cursorActiveOffset = si.cursorFrameOffsets[targetFrame];
	Rage128_SetCursorPosition(sCursorAnimation.lastX, sCursorAnimation.lastY);

	if (!sCursorAnimation.running) {
		if (targetFrame + 1 < si.cursorFrameCount)
			sCursorAnimation.nextUploadFrame = targetFrame + 1;
		else
			sCursorAnimation.nextUploadFrame = 0;

		if (sCursorAnimation.uploadedFrameCount < targetFrame + 1)
			sCursorAnimation.uploadedFrameCount = targetFrame + 1;
	}

	return B_OK;
}


bool
Rage128_SupportsCursorAnimation(void)
{
	return gInfo.sharedInfo != NULL && gInfo.sharedInfo->cursorFrameCount >= 2;
}


uint32
Rage128_CursorSupportedFrames(void)
{
	if (!Rage128_SupportsCursorAnimation())
		return 0;
	return gInfo.sharedInfo->cursorFrameCount - 1;
}


status_t
Rage128_StartCursorAnimation(void)
{
	if (!Rage128_SupportsCursorAnimation())
		return B_NOT_SUPPORTED;
	if (sCursorAnimation.uploadedFrameCount < 2)
		return B_BAD_VALUE;
	if (rage128_ensure_animation_thread() != B_OK)
		return B_ERROR;

	SharedInfo& si = *gInfo.sharedInfo;
	sCursorAnimation.currentFrame = 0;
	si.cursorActiveOffset = si.cursorFrameOffsets[0];
	Rage128_SetCursorPosition(sCursorAnimation.lastX, sCursorAnimation.lastY);

	sCursorAnimation.running = true;
	if (sCursorAnimation.semaphore >= B_OK)
		release_sem(sCursorAnimation.semaphore);
	return B_OK;
}


status_t
Rage128_StopCursorAnimation(void)
{
	if (!Rage128_SupportsCursorAnimation())
		return B_NOT_SUPPORTED;

	sCursorAnimation.running = false;
	sCursorAnimation.currentFrame = 0;
	sCursorAnimation.nextUploadFrame = 0;

	SharedInfo& si = *gInfo.sharedInfo;
	si.cursorActiveOffset = si.cursorFrameOffsets[0];
	Rage128_SetCursorPosition(sCursorAnimation.lastX, sCursorAnimation.lastY);
	return B_OK;
}


status_t
Rage128_SetCursorAnimationSpeed(float speedFactor)
{
	if (!Rage128_SupportsCursorAnimation())
		return B_NOT_SUPPORTED;
	if (!isfinite(speedFactor) || speedFactor <= 0.0f)
		return B_BAD_VALUE;

	sCursorAnimation.speedFactor = speedFactor;
	sCursorAnimation.frameInterval = (bigtime_t)(1000000.0f / speedFactor);
	if (sCursorAnimation.frameInterval < 5000)
		sCursorAnimation.frameInterval = 5000;
	return B_OK;
}


status_t
Rage128_SetCursorAnimationStartDelay(uint32 startDelayMs)
{
	if (!Rage128_SupportsCursorAnimation())
		return B_NOT_SUPPORTED;
	sCursorAnimation.startDelay = (bigtime_t)startDelayMs * 1000;
	return B_OK;
}


void
Rage128_CursorAnimationUninit(void)
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
	sCursorAnimation.uploadedFrameCount = 1;
	sCursorAnimation.nextUploadFrame = 0;
	sCursorAnimation.currentFrame = 0;
	sCursorAnimation.semaphore = -1;
	sCursorAnimation.thread = -1;
}
