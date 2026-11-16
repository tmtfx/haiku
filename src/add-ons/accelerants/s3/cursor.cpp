/*
	Copyright 1999, Be Incorporated.   All Rights Reserved.
	This file may be used under the terms of the Be Sample Code License.

	Other authors:
	Gerald Zajac 2007-2008
	Fabio Tomat 2026
*/
#include <cstdlib>
#include <cstring>
#include <math.h>
#include <OS.h>
#include "accel.h"


// Animazione cursore hardware: stesso schema di kyro/radeon_hd/ATI/Matrox.
// Condivisa fra le 3 famiglie S3 (Savage/Trio64/ViRGE) perche' SetCursorBitmap
// stesso e' gia' condiviso - solo il registro di indirizzo superficie viene
// scritto in modo diverso per famiglia (gInfo.SetCursorFrameAddress).
namespace {

struct cursor_animation_state {
	bool		running;
	bool		terminate;
	float		speedFactor;
	bigtime_t	frameInterval;
	bigtime_t	startDelay;
	uint32		uploadedFrameCount;
	uint32		nextUploadFrame;
	uint32		currentFrame;
	sem_id		semaphore;
	thread_id	thread;
};

cursor_animation_state sCursorAnimation = {
	false, false, 10.0f, 100000, 0, 1, 0, 0, -1, -1
};

} // namespace


static status_t
s3_cursor_animation_thread(void*)
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
		if (gInfo.SetCursorFrameAddress != NULL)
			gInfo.SetCursorFrameAddress(si.cursorFrameOffsets[sCursorAnimation.currentFrame]);
	}

	return B_OK;
}


static status_t
s3_ensure_animation_thread(void)
{
	if (sCursorAnimation.thread >= B_OK)
		return B_OK;

	sCursorAnimation.semaphore = create_sem(0, "s3 cursor ani");
	if (sCursorAnimation.semaphore < B_OK)
		return sCursorAnimation.semaphore;

	sCursorAnimation.terminate = false;
	sCursorAnimation.thread = spawn_thread(s3_cursor_animation_thread,
		"s3 cursor ani", B_DISPLAY_PRIORITY, NULL);
	if (sCursorAnimation.thread < B_OK) {
		delete_sem(sCursorAnimation.semaphore);
		sCursorAnimation.semaphore = -1;
		return sCursorAnimation.thread;
	}

	resume_thread(sCursorAnimation.thread);
	return B_OK;
}

uint32
GetCursorBits(void)
{
    // 2 bpp: supporta cursore bitmap, ma non drag'n'drop (no true color cursor)
    return 2;
}

status_t
SetCursorShape(uint16 width, uint16 height, uint16 hot_x, uint16 hot_y,
				uint8* andMask, uint8* xorMask)
{
	// NOTE: Currently, for BeOS, cursor width and height must be equal to 16.

	if ((width != 16) || (height != 16)) {
		return B_ERROR;
	} else if ((hot_x >= width) || (hot_y >= height)) {
		return B_ERROR;
	} else {
		// Update cursor variables appropriately.

		SharedInfo& si = *gInfo.sharedInfo;
		si.cursorHotX = hot_x;
		si.cursorHotY = hot_y;

		if ( ! gInfo.LoadCursorImage(width, height, andMask, xorMask))
			return B_ERROR;
	}

	return B_OK;
}

status_t
SetCursorBitmap(uint16 width, uint16 height, uint16 hot_x, uint16 hot_y,
                color_space colorSpace, uint16 bytesPerRow, const uint8* bitmapData)
{
    SharedInfo& si = *gInfo.sharedInfo;
    
    if (width > 64 || height > 64 || hot_x >= width || hot_y >= height)
        return B_ERROR;

    si.cursorHotX = hot_x;
    si.cursorHotY = hot_y;

    if (s3_ensure_animation_thread() != B_OK)
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

    // 1. INIZIALIZZAZIONE STRUTTURA HARDWARE A 16 BYTE PER RIGA
    // Ogni riga ha 4 blocchi. Ogni blocco è formato da 2 byte AND (0xFF) e 2 byte XOR (0x00)
    for (int y = 0; y < 64; y++) {
        uint8* rowPtr = fbCursor + (y * 16);
        for (int block = 0; block < 4; block++) {
            rowPtr[block * 4 + 0] = 0xFF; // AND byte 0
            rowPtr[block * 4 + 1] = 0xFF; // AND byte 1
            rowPtr[block * 4 + 2] = 0x00; // XOR byte 0
            rowPtr[block * 4 + 3] = 0x00; // XOR byte 1
        }
    }

    // 2. COPIAMO I PIXEL COSTRUENDO L'INTERLACCIAMENTO A WORD (16-bit)
    if (colorSpace == B_RGBA32 || colorSpace == B_RGB32) {
        for (int y = 0; y < height && y < 64; y++) {
            const uint8* srcRow = bitmapData + y * bytesPerRow;
            uint8* rowPtr = fbCursor + (y * 16);

            for (int x = 0; x < width && x < 64; x++) {
                const uint8* pixel = srcRow + x * 4;
                
                uint8 b = pixel[0];
                uint8 g = pixel[1];
                uint8 r = pixel[2];
                uint8 a = (colorSpace == B_RGBA32) ? pixel[3] : 0xFF;

                if (a < 100)
                    continue; // Salta il trasparente

                // Determiniamo in quale blocco da 16 pixel ci troviamo (0, 1, 2 o 3)
                int block = x / 16;
                // Determiniamo se siamo nel primo o nel secondo byte del blocco (0 o 1)
                int byteOffset = (x % 16) / 8;
                int bitShift = 7 - (x % 8);

                // Calcoliamo gli indici esatti in VRAM per l'AND e lo XOR di questa Word
                int andIdx = (block * 4) + byteOffset;
                int xorIdx = (block * 4) + byteOffset + 2; // Lo XOR è traslato di 2 byte rispetto all'AND

                // Spegniamo l'AND (pixel opaco)
                rowPtr[andIdx] &= ~(1 << bitShift);

                // Colore del pixel
                uint32 luma = (r + g + b) / 3;
                if (luma > 128) {
                    // Bianco -> AND=0, XOR=0
                    rowPtr[xorIdx] &= ~(1 << bitShift);
                } else {
                    // Nero -> AND=0, XOR=1
                    rowPtr[xorIdx] |= (1 << bitShift);
                }
            }
        }
    } else {
        return B_ERROR;
    }

    // 3. INDIRIZZO SUPERFICIE: dispatch per-famiglia (ordine/maschera dei
    // registri 0x4c/0x4d differiscono tra Savage, Trio64 e ViRGE - vedi le
    // rispettive *_SetCursorFrameAddress()).
    if (gInfo.SetCursorFrameAddress != NULL)
        gInfo.SetCursorFrameAddress(si.cursorFrameOffsets[targetFrame]);

    // Impostazione Palette Stack
    ReadCrtcReg(0x45);
    WriteCrtcReg(0x4a, 0); WriteCrtcReg(0x4a, 0); WriteCrtcReg(0x4a, 0); // Foreground Nero

    ReadCrtcReg(0x45);
    WriteCrtcReg(0x4b, ~0); WriteCrtcReg(0x4b, ~0); WriteCrtcReg(0x4b, ~0); // Background Bianco

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
SupportsCursorAnimation(void)
{
	return gInfo.sharedInfo != NULL && gInfo.sharedInfo->cursorFrameCount >= 2;
}


uint32
CursorSupportedFrames(void)
{
	if (!SupportsCursorAnimation())
		return 0;
	return gInfo.sharedInfo->cursorFrameCount - 1;
}


status_t
StartCursorAnimation(void)
{
	if (!SupportsCursorAnimation())
		return B_NOT_SUPPORTED;
	if (sCursorAnimation.uploadedFrameCount < 2)
		return B_BAD_VALUE;
	if (s3_ensure_animation_thread() != B_OK)
		return B_ERROR;

	SharedInfo& si = *gInfo.sharedInfo;
	sCursorAnimation.currentFrame = 0;
	if (gInfo.SetCursorFrameAddress != NULL)
		gInfo.SetCursorFrameAddress(si.cursorFrameOffsets[0]);

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

	SharedInfo& si = *gInfo.sharedInfo;
	if (gInfo.SetCursorFrameAddress != NULL)
		gInfo.SetCursorFrameAddress(si.cursorFrameOffsets[0]);
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


status_t
SetCursorAnimationStartDelay(uint32 startDelayMs)
{
	if (!SupportsCursorAnimation())
		return B_NOT_SUPPORTED;
	sCursorAnimation.startDelay = (bigtime_t)startDelayMs * 1000;
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
	sCursorAnimation.uploadedFrameCount = 1;
	sCursorAnimation.nextUploadFrame = 0;
	sCursorAnimation.currentFrame = 0;
	sCursorAnimation.semaphore = -1;
	sCursorAnimation.thread = -1;
}


void
MoveCursor(uint16 xPos, uint16 yPos)
{
	// Move the cursor to the specified position on the desktop.  If we're
	// using some kind of virtual desktop, adjust the display start position
	// accordingly and position the cursor in the proper "virtual" location.

	int x = xPos;		// use signed int's since chip specific functions
	int y = yPos;		// need signed int to determine if cursor off screen

	SharedInfo& si = *gInfo.sharedInfo;
	DisplayModeEx& dm = si.displayMode;

	uint16 hds = dm.h_display_start;	// current horizontal starting pixel
	uint16 vds = dm.v_display_start;	// current vertical starting line

	// Clamp cursor to virtual display.
	if (x >= dm.virtual_width)
		x = dm.virtual_width - 1;
	if (y >= dm.virtual_height)
		y = dm.virtual_height - 1;

	// Adjust h/v display start to move cursor onto screen.
	if (x >= (dm.timing.h_display + hds))
		hds = x - dm.timing.h_display + 1;
	else if (x < hds)
		hds = x;

	if (y >= (dm.timing.v_display + vds))
		vds = y - dm.timing.v_display + 1;
	else if (y < vds)
		vds = y;

	// Reposition the desktop on the display if required.
	if (hds != dm.h_display_start || vds != dm.v_display_start)
		MoveDisplay(hds, vds);

	// Put cursor in correct physical position.
	x -= (hds + si.cursorHotX);
	y -= (vds + si.cursorHotY);

	// Position the cursor on the display.
	gInfo.SetCursorPosition(x, y);
}

