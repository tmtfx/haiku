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
	OUTREG(R128_CUR_OFFSET, si.cursorOffset + yOffset * 16);
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
	//	 0    0		 White pixel
	//	 0    1		 Black pixel
	//	 1    0		 Screen color (for transparency)
	//	 1    1		 Reverse screen color to black or white

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
    uint8* fbCursor = (uint8*)((addr_t)si.videoMemAddr + si.cursorOffset);
    if (fbCursor == NULL)
        return B_NO_INIT;

    // 1. INIZIALIZZAZIONE PLANARE (512 byte AND, 512 byte XOR)
    // Cursore totalmente trasparente: AND = 0xFF (tutti 1), XOR = 0x00 (tutti 0)
    memset(fbCursor, 0xFF, 512);       // Piano AND
    memset(fbCursor + 512, 0x00, 512); // Piano XOR

    // 2. COPIAMO I PIXEL SUI DUE PIANI SEPARATI
    if (colorSpace == B_RGBA32 || colorSpace == B_RGB32) {
        for (int y = 0; y < height && y < 64; y++) {
            const uint8* srcRow = bitmapData + y * bytesPerRow;
            
            // Puntatori all'inizio della riga corrente nei rispettivi piani
            uint8* andRow = fbCursor + (y * 8);
            uint8* xorRow = fbCursor + 512 + (y * 8);

            for (int x = 0; x < width && x < 64; x++) {
                const uint8* pixel = srcRow + x * 4;
                
                uint8 b = pixel[0];
                uint8 g = pixel[1];
                uint8 r = pixel[2];
                uint8 a = (colorSpace == B_RGBA32) ? pixel[3] : 0xFF;

                if (a < 128)
                    continue; // Salta, resta trasparente (AND=1, XOR=0)

                int byteIdx = x / 8;
                int bitShift = 7 - (x % 8);

                // Pixel opaco: spegniamo il bit nel piano AND
                andRow[byteIdx] &= ~(1 << bitShift);

                // Calcoliamo la luminosità
                uint32 luma = (r * 77 + g * 150 + b * 29) >> 8;
                if (luma > 128) {
                    // Bianco -> AND=0, XOR=0 (Lo XOR è già a 0 dall'inizializzazione)
                    xorRow[byteIdx] &= ~(1 << bitShift);
                } else {
                    // Nero -> AND=0, XOR=1
                    xorRow[byteIdx] |= (1 << bitShift);
                }
            }
        }
    } else {
        return B_ERROR;
    }

    // 3. AGGIORNAMENTO REGISTRI COLORE CURSORE (Rage 128)
    OUTREG(R128_CUR_CLR0, 0xFFFFFF); // Background Bianco
    OUTREG(R128_CUR_CLR1, 0x000000); // Foreground Nero

    return B_OK;
}
