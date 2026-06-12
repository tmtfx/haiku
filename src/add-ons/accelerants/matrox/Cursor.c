/*
	Copyright 1999, Be Incorporated.   All Rights Reserved.
	This file may be used under the terms of the Be Sample Code License.

	Other authors:
	Mark Watson,
	Rudolf Cornelissen 4/2003-11/2004,
	Fabio Tomat 2026
*/

#define MODULE_BIT 0x20000000

/*DUALHEAD notes - 
	No hardware cursor possible on the secondary head :( 
		Reasons:
		CRTC1 has a cursor, can be displayed on DAC or MAVEN
		CRTC2 has no cursor
		Can not switch CRTC in one vblank (has to resync)
		CRTC2 does not support split screen
		app_server does not support some modes with and some without cursor
	virtual not supported, because of MAVEN blanking issues
*/

#include "acc_std.h"
#include "mga_macros.h"

static status_t program_old_matrox_cursor(uint16 width, uint16 height, uint16 bytesPerRow, const uint8* bitmapData)
{
	uint8 temp_buf[1024];

	memset(temp_buf,	   0xFF, 512); 
	memset(temp_buf + 512, 0x00, 512); 

	const uint8* src = (const uint8*)bitmapData;

	// Pixel conversion from B_RGBA32 to 1-bit AND/XOR
	for (int y = 0; y < height && y < 64; y++) {
		const uint8* srcRow = src + (y * bytesPerRow);
		uint8* andRowPtr = temp_buf + (y * 8);
		uint8* xorRowPtr = temp_buf + 512 + (y * 8);

		for (int x = 0; x < width && x < 64; x++) {
			const uint8* pixel = srcRow + (x * 4);
			uint8 b = pixel[0];
			uint8 g = pixel[1];
			uint8 r = pixel[2];
			uint8 a = pixel[3];

			if (a < 100)
				continue; // keep transparent

			int byteOffset = x / 8;
			int bitShift = 7 - (x % 8);

			// Solid pixel: turn off AND
			andRowPtr[byteOffset] &= ~(1 << bitShift);

			// White/Black selection via via Luma
			uint32 luma = (r + g + b) / 3;
			if (luma > 128) {
				// WHITE (AND = 0, XOR = 1)
				xorRowPtr[byteOffset] |= (1 << bitShift);
			} else {
				// BLACK (AND = 0, XOR = 0). XOR is already zero.
			}
		}
	}

	// Send accumulated data into the Millennium's RAMDAC TVP3026
	// Ask RAMDAC to start writing from index 0
	DACW(TVP_CUROVRWTADD, 0x00);

	// Send 1024 bytes in the cursor's data port
	for (int i = 0; i < 1024; i++) {
		DACW(TVP_CURRAMDATA, temp_buf[i]);
	}

	// Activate cursor at the RAMDAC level if visible
	uint8 cur_ctrl = DACR(TVP_DIRCURCTRL);
	cur_ctrl &= ~0x0C; // Clear old interleaved modes
	cur_ctrl |= 0x03;  // Enable cursor in standard dual-plane mode (AND/XOR)
	DACW(TVP_DIRCURCTRL, cur_ctrl);

	return B_OK;
}

static status_t matrox_set_cursor_bitmap_gseries(uint16 width, uint16 height, uint16 bytesPerRow, const uint8* bitmapData)
{
	if (width > 64 || height > 64)
		return B_ERROR;

	// Hardware cursor virtual address starts at the beginning of the VRAM
	vuint8 * dest;
    int i;

    dest = (vuint8*) si->framebuffer;
	if (dest == NULL) return B_NO_INIT;

	// Avoid flickering
	uint8 curctrl = DXIR(CURCTRL);
	DXIW(CURCTRL, curctrl & ~0x01); 

	
	// Reset HC to 0 for transparency
    // AND=0 and XOR=0 means transparency
    for (i = 0; i < 1024 ; i++) {
        dest[i] = 0x00; 
    }

	const uint8* src = (const uint8*)bitmapData;

	// 3. Conversione dei pixel da B_RGBA32 al formato a 2-bit della Matrox
	// 3. COLOR CONVERSION RGBA32 TO 3 COLORS + TRANSPARENCY
    for (uint32 y = 0; y < height && y < 64; y++) {

        vuint32* word32_row = (vuint32*)(&dest[y * 16]);

        for (uint32 x = 0; x < width && x < 64; x++) {
            const uint8* pixel = src + (y * bytesPerRow) + (x * 4);
            uint8 b = pixel[0];
            uint8 g = pixel[1];
            uint8 r = pixel[2];
            uint8 a = pixel[3];

            uint8 val = 0; // Default: 00 (Transparent)

            if (a >= 100) { // Not transparent
                uint32 luma = (r + g + b) / 3;
                if (a < 200) {
                    val = 3; // Partial transparency -> Grey/shadow
                } else {
                    val = (luma > 128) ? 1 : 2; // 01 (white) o 10 (black)
                }
            }
            
            uint8 bit0 = val & 0x01;
            uint8 bit1 = (val >> 1) & 0x01;

            uint32 bloc32_x = x / 32;       

            // --- LA CORREZIONE GEOMETRICA PER LE MATROX G-SERIES ---
            // Reassembling... correct orientation:
            // geometric byte-swap.
            uint32 bloc32_pos = bloc32_x ^ 1; 


            // Matrox wants MSB at left.

            uint8 bitShift = 31 - (x % 32); 

            word32_row[bloc32_pos] &= ~(1 << bitShift);
            word32_row[bloc32_pos] |= (bit0 << bitShift);

            word32_row[bloc32_pos + 2] &= ~(1 << bitShift);
            word32_row[bloc32_pos + 2] |= (bit1 << bitShift);
        }
    }
	// Set Hardware Cursor Palette via extended RAMDAC (Macro DXIW)
	
	// Color 0: White
	DXIW(CURCOL0RED,   0xFF);
	DXIW(CURCOL0GREEN, 0xFF);
	DXIW(CURCOL0BLUE,  0xFF);

	// Color 1: Black
	DXIW(CURCOL1RED,   0x00);
	DXIW(CURCOL1GREEN, 0x00);
	DXIW(CURCOL1BLUE,  0x00);

	// Color 2: Grey
	DXIW(CURCOL2RED,   0x88);
	DXIW(CURCOL2GREEN, 0x88);
	DXIW(CURCOL2BLUE,  0x88);

	// FINAL CONFIGURATION
	// Set cursor in True Color 64x64 mode.
	// Bit 0: Enable Hardware Crusror
	// Bit 1-3: Cursor mode. `0x04` means "3 colors+transparency 64x64" mode.
	
	curctrl &= ~0x0E; // Clear old modes
	curctrl |= 0x04;  // Set True Color 64x64 mode
	
	// if it had to be visible, enable keeping bit 0
	if (si->cursor.is_visible) {
		curctrl |= 0x01;
	}

	DXIW(CURCTRL, curctrl);

	return B_OK;
}

status_t SET_CURSOR_SHAPE(uint16 width, uint16 height, uint16 hot_x, uint16 hot_y, uint8 *andMask, uint8 *xorMask) 
{
	LOG(4,("SET_CURSOR_SHAPE: width %d, height %d\n", width, height));
	if ((width != 16) || (height != 16))
	{
		return B_ERROR;
	}
	else if ((hot_x >= width) || (hot_y >= height))
	{
		return B_ERROR;
	}
	else
	{
		gx00_crtc_cursor_define(andMask,xorMask);

		/* Update cursor variables appropriately. */
		si->cursor.width = width;
		si->cursor.height = height;
		si->cursor.hot_x = hot_x;
		si->cursor.hot_y = hot_y;
	}

	return B_OK;
}

/* Move the cursor to the specified position on the desktop, taking account of virtual/dual issues */
void MOVE_CURSOR(uint16 x, uint16 y) 
{
	uint16 hds = si->dm.h_display_start;	/* the current horizontal starting pixel */
	uint16 vds = si->dm.v_display_start;	/* the current vertical starting line */
	uint16 h_adjust;					 

	/* clamp cursor to display */
	if (x >= si->dm.virtual_width) x = si->dm.virtual_width - 1;
	if (y >= si->dm.virtual_height) y = si->dm.virtual_height - 1;

	/* store, for our info */
	si->cursor.x = x;
	si->cursor.y = y;

	/*set up minimum amount to scroll*/
	if (si->dm.flags & DUALHEAD_BITS)
	{
		switch(si->dm.space)
		{
		case B_RGB16_LITTLE:
			h_adjust = 0x1f;
			break;
		case B_RGB32_LITTLE:
			h_adjust = 0x0f;
			break;
		default:
			h_adjust = 0x1f;
			break;
		}
	}
	else
	{
		switch(si->dm.space)
		{
		case B_CMAP8:
			h_adjust = 0x07;
			break;
		case B_RGB15_LITTLE:case B_RGB16_LITTLE:
			h_adjust = 0x03;
			break;
		case B_RGB32_LITTLE:
			h_adjust = 0x01;
			break;
		default:
			h_adjust = 0x07;
			break;
		}
	}

	/* adjust h/v_display_start to move cursor onto screen */
	switch (si->dm.flags & DUALHEAD_BITS)
	{
	case DUALHEAD_ON:
	case DUALHEAD_SWITCH:
		if (x >= ((si->dm.timing.h_display * 2) + hds))
		{
			hds = ((x - (si->dm.timing.h_display * 2)) + 1 + h_adjust) & ~h_adjust;
			/* make sure we stay within the display! */
			if ((hds + (si->dm.timing.h_display * 2)) > si->dm.virtual_width)
				hds -= (h_adjust + 1);
		}
		else if (x < hds)
			hds = x & ~h_adjust;
		break;
	default:
		if (x >= (si->dm.timing.h_display + hds))
		{
			hds = ((x - si->dm.timing.h_display) + 1 + h_adjust) & ~h_adjust;
			/* make sure we stay within the display! */
			if ((hds + si->dm.timing.h_display) > si->dm.virtual_width)
				hds -= (h_adjust + 1);
		}
		else if (x < hds)
			hds = x & ~h_adjust;
		break;
	}

	if (y >= (si->dm.timing.v_display + vds))
		vds = y - si->dm.timing.v_display + 1;
	else if (y < vds)
		vds = y;

	/* reposition the desktop _and_ the overlay on the display if required */
	if ((hds!=si->dm.h_display_start) || (vds!=si->dm.v_display_start))
	{
		MOVE_DISPLAY(hds,vds);
		gx00_bes_move_overlay();
	}

	/* put cursor in correct physical position */
	x -= hds + si->cursor.hot_x;
	y -= vds + si->cursor.hot_y;

	/* account for switched CRTC's */
	if (si->switched_crtcs)	x -= si->dm.timing.h_display;

	/* position the cursor on the display */
	gx00_crtc_cursor_position(x,y);
}

void SHOW_CURSOR(bool is_visible) 
{
	/* record for our info */
	si->cursor.is_visible = is_visible;

	if (is_visible)
		gx00_crtc_cursor_show();
	else
		gx00_crtc_cursor_hide();
}

uint32 GET_CURSOR_BITS(void)
{
	//  MIL1, MYST, MIL2 = 1 bit (black or white) + trnasparency
	//  G100 - G400MAX = 2 bits (3 colors at 24bit palette) + transparency
	//  G450 - G550 = 2 bits (3 colors at 24bit palette) + transparency, handled indipendently for CRTC1 and CRTC2
	if (si->card_type < 3) return 1; // MIL1, MYST, MIL2
	return 2;
}

status_t SET_CURSOR_BITMAP(uint16 width, uint16 height, uint16 hotX, uint16 hotY, color_space colorSpace, uint16 bytesPerRow, const uint8* bitmapData)
{
	if (width > 64 || height > 64)
		return B_ERROR;

	(void)colorSpace;

	si->cursor.hot_x = hotX;
	si->cursor.hot_y = hotY;
	si->cursor.width = width;
	si->cursor.height = height;

	// 2. Bivio generazionale
	if (si->ps.card_type < G100) {
		// ==============================================
		// STRATEGIA ERA PRE-G (MIL1, MYST, MIL2) - 1 bit
		// ==============================================
		
		// NOTE: first series (pre-G100) hardware cursor is inside DAC
		
		return program_old_matrox_cursor(width, height, bytesPerRow, bitmapData);

	} else {
		// ============================================
		// SERIES G (G100 - G550) - 2 bit - RGB palette
		// ============================================
		
		// NOTE: Hardware cursor is in the reserved VRAM's first KB
		return matrox_set_cursor_bitmap_gseries(width, height, bytesPerRow, bitmapData);
	}
}
