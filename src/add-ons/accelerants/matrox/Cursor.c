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

	// L'indirizzo virtuale corrisponde all'inizio della VRAM clonato dall'accelerante
	vuint8 * dest;
    int i;

    // Ottieni il puntatore alla memoria del cursore
    dest = (vuint8*) si->framebuffer;
	if (dest == NULL) return B_NO_INIT;

	// Spegniamo temporaneamente il cursore per evitare sfarfallii (flicker) 
	// o corruzioni della cache della CPU mentre scriviamo in VRAM.
	// Leggiamo il registro di controllo corrente, tenendo spento il bit di abilitazione.
	uint8 curctrl = DXIR(CURCTRL);
	DXIW(CURCTRL, curctrl & ~0x01); 

	
	/* 1. Resetta completamente i 1024 byte del cursore hardware a zero.
     * Nelle Matrox, AND=0 e XOR=0 significa colore di trasparenza totale.
     * Questo eliminerà istantaneamente il quadrato grigio di sfondo.
     */
    for (i = 0; i < 1024 ; i++) {
        dest[i] = 0x00; 
    }

	const uint8* src = (const uint8*)bitmapData;

	// 3. Conversione dei pixel da B_RGBA32 al formato a 2-bit della Matrox
	for (uint32 y = 0; y < height && y < 64; y++) {
        /* row punta all'inizio dei 16 byte della riga corrente nel framebuffer */
        vuint8* row = &dest[y * 16];

        for (uint32 x = 0; x < width && x < 64; x++) {
            const uint8* pixel = src + (y * bytesPerRow) + (x * 4);
            uint8 b = pixel[0];
            uint8 g = pixel[1];
            uint8 r = pixel[2];
            uint8 a = pixel[3];

            uint8 val = 0; // Default: 11 in binario (Trasparente)

            if (a >= 100) { // Se il pixel non è trasparente...
                uint32 luma = (r + g + b) / 3;
                if (a < 200) {
                    val = 3; // Pixel semitrasparente -> Grigio/Ombra (Colore 2 = 10 in binario)
                } else {
                    val = (luma > 128) ? 1 : 2; // Colore 0 (00 binario) o Colore 1 (01 binario)
                }
            }
            /*
            // COSTRUZIONE DELLA PAROLA A 16-BIT PER LA MATROX
            // Il bit 1 di 'val' va nel byte superiore, il bit 0 va nel byte inferiore.
            //
            uint16 pixel_bits_w;
            pixel_bits_w = ((uint16)(val >> 1) << 8) | (uint16)(val & 0x01);

            // CALCOLO DELLA POSIZIONE NELLA RIGA
            // Ogni parola a 16-bit contiene i dati per 16 pixel hardware.
            //
            uint32 word_pos = x / 16;
            
            // IL TRUCCO PER AGGIUSTARE LA MANO SPEZZATA:
            // Visto che la mano appare divisa in due e invertita, applichiamo 
            // uno XOR (^ 1) sulla posizione della parola. Questo inverte l'ordine 
            // dei blocchi di pixel (il blocco di sinistra va a destra e viceversa), 
            // riassemblando la mano in modo geometricamente corretto.
            //
            word_pos ^= 1; 

            uint8 bitShift = (x % 16); // Posizione del bit all'interno della parola

            // Cancelliamo i bit precedenti in quella posizione e inseriamo i nuovi
            row[word_pos] &= ~(1 << bitShift);
            row[word_pos] |= (pixel_bits_w << bitShift);
            */
            

            // --- MAPPA DEI BIT STRUTTURATA MATROX ---
            // Isoliamo i due singoli bit del nostro valore 'val'
            // bit0 = val & 0x01
            // bit1 = (val >> 1) & 0x01
            //
            uint8 bit0 = val & 0x01;
            uint8 bit1 = (val >> 1) & 0x01;

            // All'interno della riga, 64 pixel divisi in 8 byte significa 
            // che ogni byte contiene 8 pixel (1 bit ciascuno).
            // Troviamo l'indice del byte (da 0 a 7) e la posizione del bit.
            // Matrox vuole il pixel 0 sul bit più significativo (MSB = 7) 
            //uint32 bitPosition = x / 8;       // Determina il byte (0..7)
            //uint8  bitShift    = 7 - (x % 8); // Inversione MSB->LSB per l'ordine dei pixel
            //Calcoliamo la posizione standard del byte (0..7)
            uint32 baseBytePosition = x / 8;       
            
            /* --- IL CORRETTIVO PER COMPENSARE LO SPOSTAMENTO A DESTRA ---
             * Invertiamo l'ordine dei byte all'interno del piano da 8 byte.
             * Il byte 0 diventa 7, il byte 1 diventa 6, ecc.
             * Questo sposterà la manina da destra a sinistra.
             */
            uint32 bitPosition = 7 - baseBytePosition; 

            // Mantieni l'ordine standard dei bit all'interno del byte
            uint8 bitShift = (x % 8);

            // Scriviamo il Bit 0 nel piano inferiore (byte 0..7)
            row[bitPosition] &= ~(1 << bitShift);
            row[bitPosition] |= (bit0 << bitShift);

            // Scriviamo il Bit 1 nel piano superiore (byte 8..15)
            row[bitPosition + 8] &= ~(1 << bitShift);
            row[bitPosition + 8] |= (bit1 << bitShift);

        }
    }

	// 4. Programmazione della Palette del Cursore Hardware via RAMDAC esteso (Macro DXIW)
	
	// Colore 0: Bianco (Usato per l'interno della freccia)
	DXIW(CURCOL0RED,   0xFF);
	DXIW(CURCOL0GREEN, 0xFF);
	DXIW(CURCOL0BLUE,  0xFF);

	// Colore 1: Nero (Usato per il bordo del cursore)
	DXIW(CURCOL1RED,   0x00);
	DXIW(CURCOL1GREEN, 0x00);
	DXIW(CURCOL1BLUE,  0x00);

	// Colore 2: Grigio (Per l'effetto ombra/anti-aliasing)
	DXIW(CURCOL2RED,   0x88);
	DXIW(CURCOL2GREEN, 0x88);
	DXIW(CURCOL2BLUE,  0x88);

	// 5. Configurazione finale del registro di controllo del cursore (`MGADXI_CURCTRL`)
	// Dobbiamo assicurarci di impostare il cursore in modalità 64x64 True Color.
	// Sulla serie G:
	// Bit 0: Cursore Hardware Abilitato (lo riaccendiamo se era acceso prima, o lo lasciamo gestire a SHOW_CURSOR)
	// Bit 1-3: Modalità del Cursore. Il valore `0x04` seleziona la modalità "64x64 a 3 colori con trasparenza".
	
	curctrl &= ~0x0E; // Puliamo i bit di modalità vecchi (bit 1, 2, 3)
	curctrl |= 0x04;  // Impostiamo la modalità True Color 64x64
	
	// Se il cursore doveva essere visibile, riaccendiamolo preservando il bit 0
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
