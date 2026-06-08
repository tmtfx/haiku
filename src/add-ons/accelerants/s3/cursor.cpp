/*
	Copyright 1999, Be Incorporated.   All Rights Reserved.
	This file may be used under the terms of the Be Sample Code License.

	Other authors:
	Gerald Zajac 2007-2008
	Fabio Tomat 2026
*/
#include <cstdlib>
#include <cstring>
#include "accel.h"

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

    uint8* fbCursor = (uint8*)((addr_t)si.videoMemAddr + si.cursorOffset);
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

    // 3. SELEZIONE MASCHERE REGISTRI IN BASE AL CHIP
    // Gestiamo la differenza di maschera che hai giustamente notato tra Trio e le altre
    if (S3_SAVAGE_FAMILY(si.chipType)) {
        WriteCrtcReg(0x4d, (0xff & si.cursorOffset / 1024));
        WriteCrtcReg(0x4c, (0xff00 & si.cursorOffset / 1024) >> 8);
    } else {
        // Trio64 legacy
        WriteCrtcReg(0x4c, (0x0f00 & si.cursorOffset / 1024) >> 8);
        WriteCrtcReg(0x4d, (0xff & si.cursorOffset / 1024));
    }

    // Impostazione Palette Stack
    ReadCrtcReg(0x45);        
    WriteCrtcReg(0x4a, 0); WriteCrtcReg(0x4a, 0); WriteCrtcReg(0x4a, 0); // Foreground Nero

    ReadCrtcReg(0x45);        
    WriteCrtcReg(0x4b, ~0); WriteCrtcReg(0x4b, ~0); WriteCrtcReg(0x4b, ~0); // Background Bianco

    return B_OK;
}


/* ancora 2 bande con 2 cursori
status_t
SetCursorBitmap(uint16 width, uint16 height, uint16 hot_x, uint16 hot_y,
                color_space colorSpace, uint16 bytesPerRow, const uint8* bitmapData)
{
    SharedInfo& si = *gInfo.sharedInfo;
    TRACE("SetCursorBitmap FINAL: W=%u H=%u Hot=%u,%u BPR=%u\n", width, height, hot_x, hot_y, bytesPerRow);

    if (width > 64 || height > 64)
        return B_ERROR;
    
    if ((hot_x >= width) || (hot_y >= height))
        return B_ERROR;

    si.cursorHotX = hot_x;
    si.cursorHotY = hot_y;

    // Indirizzo reale della memoria video dedicata al cursore hardware S3
    uint8* fbCursor = (uint8*)((addr_t)si.videoMemAddr + si.cursorOffset);
    if (fbCursor == NULL)
        return B_NO_INIT;

    // 1. PULIZIA E INIZIALIZZAZIONE HARDWARE (64 righe * 16 byte per riga = 1024 byte totali)
    // Impostiamo ogni riga hardware con: 8 byte di AND = 0xFF e 8 byte di XOR = 0x00 (Trasparenza totale S3)
    for (int y = 0; y < 64; y++) {
        uint8* rowPtr = fbCursor + (y * 16);
        memset(rowPtr, 0xFF, 8);      // AND = 1 (0xFF) -> Trasparente
        memset(rowPtr + 8, 0x00, 8);  // XOR = 0 (0x00) -> Nessuna inversione
    }

    // 2. CONVERSIONE E COPIA DIRETTA INTERLACCIATA (Bypassa le funzioni native buggate)
    if (colorSpace == B_RGBA32 || colorSpace == B_RGB32) {
        for (int y = 0; y < height && y < 64; y++) {
            const uint8* srcRow = bitmapData + y * bytesPerRow;
            
            // Per ogni riga hardware: i primi 8 byte sono l'AND, i successivi 8 byte sono lo XOR (offset continuo +8)
            uint8* dstAndRow = fbCursor + (y * 16);
            uint8* dstXorRow = fbCursor + (y * 16) + 8;

            for (int x = 0; x < width && x < 64; x++) {
                const uint8* pixel = srcRow + x * 4;
                
                uint8 b = pixel[0];
                uint8 g = pixel[1];
                uint8 r = pixel[2];
                uint8 a = (colorSpace == B_RGBA32) ? pixel[3] : 0xFF;

                if (a < 100) {
                    // Trasparente: Lasciamo AND=1 e XOR=0 (già impostati dal memset, sovrascriviamo per sicurezza)
                    dstAndRow[x / 8] |= (1 << (7 - (x % 8)));
                    dstXorRow[x / 8] &= ~(1 << (7 - (x % 8)));
                    continue;
                }

                int byteIdx = x / 8;
                int bitShift = 7 - (x % 8);

                // Pixel visibile: abbassiamo il bit AND a 0 per disegnarlo
                dstAndRow[byteIdx] &= ~(1 << bitShift);

                // Calcolo luminosità (Luma) per decidere Bianco o Nero (Logica Trio64 corretta)
                uint32 luma = (r + g + b) / 3;

                if (luma > 128) {
                    // Pixel Chiaro -> BIANCO (S3: AND=0, XOR=0)
                    dstXorRow[byteIdx] &= ~(1 << bitShift);
                } else {
                    // Pixel Scuro o Ombra -> NERO (S3: AND=0, XOR=1)
                    dstXorRow[byteIdx] |= (1 << bitShift);
                }
            }
        }
    } else {
        return B_ERROR;
    }

    // 3. AGGIORNIAMO I REGISTRI CRTC HARDWARE PER ATTIVARE IL CURSORE
    // Usiamo le maschere e i registri specifici del modulo Trio64 per l'indirizzo VRAM e la palette.
    WriteCrtcReg(0x4c, (0x0f00 & si.cursorOffset / 1024) >> 8);
    WriteCrtcReg(0x4d, (0xff & si.cursorOffset / 1024));

    // Reset del puntatore dello stack del colore e impostazione della palette nativa (R=0, G=0, B=0 per nero; R=255, G=255, B=255 per bianco)
    ReadCrtcReg(0x45);        
    WriteCrtcReg(0x4a, 0);    // Foreground stack: R=0, G=0, B=0
    WriteCrtcReg(0x4a, 0);
    WriteCrtcReg(0x4a, 0);

    ReadCrtcReg(0x45);        
    WriteCrtcReg(0x4b, ~0);   // Background stack: R=255, G=255, B=255
    WriteCrtcReg(0x4b, ~0);
    WriteCrtcReg(0x4b, ~0);

    // Se noti che il cursore non appare, assicurati che i bit di abilitazione nel registro CR45 siano settati correttamente.

    return B_OK;
}*/


/* due cursori separati uno per trasparenza uno per colore
status_t
SetCursorBitmap(uint16 width, uint16 height, uint16 hot_x, uint16 hot_y,
                color_space colorSpace, uint16 bytesPerRow, const uint8* bitmapData)
{
    SharedInfo& si = *gInfo.sharedInfo;
    TRACE("SetCursorBitmap: w=%u h=%u hot=%u,%u bpr=%u\n", width, height, hot_x, hot_y, bytesPerRow);

    if (width > 64 || height > 64)
        return B_ERROR;
    
    if ((hot_x >= width) || (hot_y >= height))
        return B_ERROR;

    si.cursorHotX = hot_x;
    si.cursorHotY = hot_y;

    // Definiamo i confini hardware standard del cursore S3
    const uint16 hwWidth = 64;
    const uint16 hwHeight = 64;
    const int hwBytes = (hwWidth * hwHeight) / 8; // Esattamente 512 Byte per piano

    uint8* andMask = (uint8*)malloc(hwBytes);
    uint8* xorMask = (uint8*)malloc(hwBytes);
    if (!andMask || !xorMask) {
        free(andMask);
        free(xorMask);
        return B_NO_MEMORY;
    }

    // Pre-inizializziamo tutto il blocco a TRASPARENTE (AND=1, XOR=0)
    memset(andMask, 0xFF, hwBytes);
    memset(xorMask, 0x00, hwBytes);

    if (colorSpace == B_RGBA32 || colorSpace == B_RGB32) {
        for (int y = 0; y < height; y++) {
            const uint8* srcRow = bitmapData + y * bytesPerRow;
            
            // Ci muoviamo linearmente sul nostro piano da 8 byte per riga (64 pixel / 8)
            uint8* dstAndRow = andMask + (y * 8);
            uint8* dstXorRow = xorMask + (y * 8);

            for (int x = 0; x < width; x++) {
                const uint8* pixel = srcRow + x * 4;
                
                uint8 b = pixel[0];
                uint8 g = pixel[1];
                uint8 r = pixel[2];
                uint8 a = (colorSpace == B_RGBA32) ? pixel[3] : 0xFF;

                // Se il pixel dell'app_server è trasparente, lasciamo AND=1 e XOR=0
                if (a < 100)
                    continue;

                int byteIdx = x / 8;
                int bitShift = 7 - (x % 8);

                // Pixel visibile: abbassiamo la maschera AND a 0
                dstAndRow[byteIdx] &= ~(1 << bitShift);

                // Calcolo della luminosità (Luma)
                uint32 luma = (r + g + b) / 3;

                // Tabella di verità reale estratta dai tuoi registri S3:
                if (luma > 128) {
                    // Pixel Chiaro -> BIANCO (S3: AND=0, XOR=0)
                    dstXorRow[byteIdx] &= ~(1 << bitShift);
                } else {
                    // Pixel Scuro -> NERO (S3: AND=0, XOR=1)
                    dstXorRow[byteIdx] |= (1 << bitShift);
                }
            }
        }
    } else {
        free(andMask);
        free(xorMask);
        return B_ERROR;
    }

    // Invochiamo la funzione specifica passandogli la geometria fissa 64x64.
    // Il nuovo ciclo sistemato al Passo 1 digerirà questi 512+512 byte lineari in modo perfetto.
    bool ok = gInfo.LoadCursorImage(hwWidth, hwHeight, andMask, xorMask);

    free(andMask);
    free(xorMask);

    return ok ? B_OK : B_ERROR;
}*/

/*
status_t
SetCursorBitmap(uint16 width, uint16 height, uint16 hot_x, uint16 hot_y,
                color_space colorSpace, uint16 bytesPerRow, const uint8* bitmapData)
{
    SharedInfo& si = *gInfo.sharedInfo;
    TRACE("SetCursorBitmap: w=%u h=%u hot=%u,%u bpr=%u\n", width, height, hot_x, hot_y, bytesPerRow);

    if (width > 64 || height > 64)
        return B_ERROR;
    
    if ((hot_x >= width) || (hot_y >= height))
        return B_ERROR;

    si.cursorHotX = hot_x;
    si.cursorHotY = hot_y;

    // ARROTONDAMENTO CRITICO: Forziamo la larghezza hardware a un blocco di 32 pixel (4 byte) o 64 pixel (8 byte)
    // Se la manina è 22, saltiamo a 32 per far combaciare la divisione (width / 8) dentro LoadCursorImage
    const uint16 calcWidth = (width <= 32) ? 32 : 64;
    const uint16 calcHeight = (height <= 32) ? 32 : 64;

    const int bytesPerCursorRow = calcWidth / 8; // Sarà esattamente 4 o 8 byte
    const int maskBytes = bytesPerCursorRow * calcHeight;

    uint8* andMask = (uint8*)malloc(maskBytes);
    uint8* xorMask = (uint8*)malloc(maskBytes);
    if (!andMask || !xorMask) {
        free(andMask);
        free(xorMask);
        return B_NO_MEMORY;
    }

    // Di base, tutto il buffer convertito è trasparente: AND=1, XOR=0
    memset(andMask, 0xFF, maskBytes);
    memset(xorMask, 0x00, maskBytes);

    if (colorSpace == B_RGBA32 || colorSpace == B_RGB32) {
        for (int y = 0; y < height; y++) {
            const uint8* srcRow = bitmapData + y * bytesPerRow;
            
            // Usiamo il passo fisso calcolato (4 o 8 byte per riga)
            uint8* dstAnd = andMask + (y * bytesPerCursorRow);
            uint8* dstXor = xorMask + (y * bytesPerCursorRow);

            for (int x = 0; x < width; x++) {
                const uint8* pixel = srcRow + x * 4;
                
                uint8 b = pixel[0];
                uint8 g = pixel[1];
                uint8 r = pixel[2];
                uint8 a = (colorSpace == B_RGBA32) ? pixel[3] : 0xFF;
                //uint8 a = pixel[3];

                if (a < 100)
                    continue;

                int byteIdx = x / 8;
                int bitShift = 7 - (x % 8);

                // Il pixel è visibile: forziamo la maschera AND a 0
                dstAnd[byteIdx] &= ~(1 << bitShift);

                // Calcoliamo la luminosità (Luma)
                uint32 luma = (r + g + b) / 3;

                if (luma > 128) {
                    // Pixel Chiaro -> BIANCO (S3: AND=0, XOR=0)
                    dstXor[byteIdx] &= ~(1 << bitShift);
                } else {
                    // Pixel Scuro -> NERO (S3: AND=0, XOR=1)
                    dstXor[byteIdx] |= (1 << bitShift);
                }
            }
        }
    } else {
        free(andMask);
        free(xorMask);
        return B_ERROR;
    }

    // Passiamo le dimensioni arrotondate. 
    // Ora (calcWidth / 8) darà un numero intero esatto (4 o 8), svuotando il buffer
    // riga per riga senza lasciare residui che creano il groviglio.
    bool ok = gInfo.LoadCursorImage(calcWidth, calcHeight, andMask, xorMask);

    free(andMask);
    free(xorMask);

    return ok ? B_OK : B_ERROR;
}*/

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

