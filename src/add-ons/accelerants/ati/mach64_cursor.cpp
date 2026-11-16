/*
	Copyright 2009 Haiku, Inc.  All rights reserved.
	Distributed under the terms of the MIT license.

	Authors:
	Gerald Zajac 2009
*/


#include "accelerant.h"
#include "mach64.h"

#include <string.h>
#include <math.h>
#include <OS.h>


namespace {

// Animazione cursore hardware: stesso schema usato per kyro/radeon_hd/Rage128.
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
mach64_cursor_animation_thread(void*)
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
		Mach64_SetCursorPosition(sCursorAnimation.lastX, sCursorAnimation.lastY);
	}

	return B_OK;
}


static status_t
mach64_ensure_animation_thread(void)
{
	if (sCursorAnimation.thread >= B_OK)
		return B_OK;

	sCursorAnimation.semaphore = create_sem(0, "mach64 cursor ani");
	if (sCursorAnimation.semaphore < B_OK)
		return sCursorAnimation.semaphore;

	sCursorAnimation.terminate = false;
	sCursorAnimation.thread = spawn_thread(mach64_cursor_animation_thread,
		"mach64 cursor ani", B_DISPLAY_PRIORITY, NULL);
	if (sCursorAnimation.thread < B_OK) {
		delete_sem(sCursorAnimation.semaphore);
		sCursorAnimation.semaphore = -1;
		return sCursorAnimation.thread;
	}

	resume_thread(sCursorAnimation.thread);
	return B_OK;
}


void
Mach64_ShowCursor(bool bShow)
{
	// Turn cursor on/off.

   	OUTREGM(GEN_TEST_CNTL, bShow ? HWCURSOR_ENABLE : 0, HWCURSOR_ENABLE);
}


void
Mach64_SetCursorPosition(int x, int y)
{
	SharedInfo& si = *gInfo.sharedInfo;

	sCursorAnimation.lastX = x;
	sCursorAnimation.lastY = y;

	// xOffset & yOffset are used for displaying partial cursors on screen edges.

	uint8 xOffset = 0;
	uint8 yOffset = 0;

	if (x < 0) {
		xOffset = -x;
		x = 0;
	}

	if (y < 0) {
		yOffset = -y;
		y = 0;
	}

	OUTREG(CUR_OFFSET, (si.cursorActiveOffset >> 3) + (yOffset << 1));
	OUTREG(CUR_HORZ_VERT_OFF, (yOffset << 16) | xOffset);
	OUTREG(CUR_HORZ_VERT_POSN, (y << 16) | x);
}


bool
Mach64_LoadCursorImage(int width, int height, uint8* andMask, uint8* xorMask)
{
	SharedInfo& si = *gInfo.sharedInfo;

	if (andMask == NULL || xorMask == NULL)
		return false;

	uint16* fbCursor = (uint16*)((addr_t)si.videoMemAddr + si.cursorOffset);

	// Initialize the hardware cursor as completely transparent.

	memset(fbCursor, 0xaa, CURSOR_BYTES);

	// Now load the AND & XOR masks for the cursor image into the cursor
	// buffer.  Note that a particular bit in these masks will have the
	// following effect upon the corresponding cursor pixel:
	//	AND  XOR	Result
	//	 0	0		 White pixel
	//	 0	1		 Black pixel
	//	 1	0		 Screen color (for transparency)
	//	 1	1		 Reverse screen color to black or white

	for (int row = 0; row < height; row++) {
		for (int colByte = 0; colByte < width / 8; colByte++) {
			// Convert the 8 bit AND and XOR masks into a 16 bit mask containing
			// pairs of the bits from the AND and XOR maks.

			uint8 andBits = *andMask++;
			uint8 xorBits = *xorMask++;
			uint16 cursorBits = 0;

			for (int j = 0; j < 8; j++) {
				cursorBits <<= 2;
				cursorBits |= ((andBits & 0x01) << 1);
				cursorBits |= (xorBits & 0x01);
				andBits >>= 1;
				xorBits >>= 1;
			}

			fbCursor[row * 8 + colByte] = cursorBits;
		}
	}

	// Set the cursor colors which are white background and black foreground.

	OUTREG(CUR_CLR0, ~0);
	OUTREG(CUR_CLR1, 0);

	return true;
}

status_t
Mach64_SetCursorBitmap(uint16 width, uint16 height, uint16 hot_x, uint16 hot_y,
                       color_space colorSpace, uint16 bytesPerRow, const uint8* bitmapData)
{
    SharedInfo& si = *gInfo.sharedInfo;

    if (width > 64 || height > 64 || hot_x >= width || hot_y >= height)
        return B_ERROR;

    si.cursorHotX = hot_x;
    si.cursorHotY = hot_y;

    if (mach64_ensure_animation_thread() != B_OK)
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

    uint16* fbCursor = (uint16*)((addr_t)si.videoMemAddr + si.cursorFrameOffsets[targetFrame]);
    if (fbCursor == NULL)
        return B_NO_INIT;

    // INITIALIZATION TO TRANSPARENT
    for (int i = 0; i < 64 * 8; i++) {
        fbCursor[i] = 0xAAAA;
    }

    // PIXEL COPY
    if (colorSpace == B_RGBA32 || colorSpace == B_RGB32) {
        for (int y = 0; y < height && y < 64; y++) {
            const uint8* srcRow = bitmapData + y * bytesPerRow;
            uint16* rowPtr = fbCursor + (y * 8); // Y (Top-Down)

            for (int x = 0; x < width && x < 64; x++) {
                const uint8* pixel = srcRow + x * 4;
                
                uint8 b = pixel[0];
                uint8 g = pixel[1];
                uint8 r = pixel[2];
                uint8 a = (colorSpace == B_RGBA32) ? pixel[3] : 0xFF;

                if (a < 128)
                    continue; // Keep transparent (10b)

                int wordIdx = x / 8;
                
                // Chip ATI is Little-Endian
                int bitPos = (x % 8) * 2;

                // Temporary zeroing
                rowPtr[wordIdx] &= ~(0x3 << bitPos);

                // Luma
                uint32 luma = (r * 77 + g * 150 + b * 29) >> 8;
                if (luma > 128) {
                    // White -> AND=0, XOR=0 (00b) -> already zeroed
					// rowPtr[wordIdx] |= (0x2 << bitPos); <-- with this white becomes tansparent
                } else {
                    // Black -> AND=0, XOR=1 (01b)
                    rowPtr[wordIdx] |= (0x1 << bitPos);
                }
            }
        }
    } else {
        return B_ERROR;
    }

    // UPDATE CURSOR PALETTES
    OUTREG(CUR_CLR0, 0xFFFFFF00); // White - correct byte order
    OUTREG(CUR_CLR1, 0x00000000); // Black

    si.cursorActiveOffset = si.cursorFrameOffsets[targetFrame];
    Mach64_SetCursorPosition(sCursorAnimation.lastX, sCursorAnimation.lastY);

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
Mach64_SupportsCursorAnimation(void)
{
	return gInfo.sharedInfo != NULL && gInfo.sharedInfo->cursorFrameCount >= 2;
}


uint32
Mach64_CursorSupportedFrames(void)
{
	if (!Mach64_SupportsCursorAnimation())
		return 0;
	return gInfo.sharedInfo->cursorFrameCount - 1;
}


status_t
Mach64_StartCursorAnimation(void)
{
	if (!Mach64_SupportsCursorAnimation())
		return B_NOT_SUPPORTED;
	if (sCursorAnimation.uploadedFrameCount < 2)
		return B_BAD_VALUE;
	if (mach64_ensure_animation_thread() != B_OK)
		return B_ERROR;

	SharedInfo& si = *gInfo.sharedInfo;
	sCursorAnimation.currentFrame = 0;
	si.cursorActiveOffset = si.cursorFrameOffsets[0];
	Mach64_SetCursorPosition(sCursorAnimation.lastX, sCursorAnimation.lastY);

	sCursorAnimation.running = true;
	if (sCursorAnimation.semaphore >= B_OK)
		release_sem(sCursorAnimation.semaphore);
	return B_OK;
}


status_t
Mach64_StopCursorAnimation(void)
{
	if (!Mach64_SupportsCursorAnimation())
		return B_NOT_SUPPORTED;

	sCursorAnimation.running = false;
	sCursorAnimation.currentFrame = 0;
	sCursorAnimation.nextUploadFrame = 0;

	SharedInfo& si = *gInfo.sharedInfo;
	si.cursorActiveOffset = si.cursorFrameOffsets[0];
	Mach64_SetCursorPosition(sCursorAnimation.lastX, sCursorAnimation.lastY);
	return B_OK;
}


status_t
Mach64_SetCursorAnimationSpeed(float speedFactor)
{
	if (!Mach64_SupportsCursorAnimation())
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
Mach64_SetCursorAnimationStartDelay(uint32 startDelayMs)
{
	if (!Mach64_SupportsCursorAnimation())
		return B_NOT_SUPPORTED;
	sCursorAnimation.startDelay = (bigtime_t)startDelayMs * 1000;
	return B_OK;
}


void
Mach64_CursorAnimationUninit(void)
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