/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include <math.h>
#include <OS.h>
#include <string.h>
#include "sm750_macros.h"
#include "DriverInterface.h"
#include "protos.h"


#define CALLED() debug_printf("SM750_ACC: CALLED %s\n", __FUNCTION__)

extern accelerant_info *gInfo;

bool actualoutbounds = false;
bool previousoutbounds = false;


// Animazione cursore hardware: stesso schema usato per kyro/radeon_hd/ATI/
// Matrox/S3/Trident/XGI. Un'unica macchina a stati, perche' le due modalita'
// cursore di questo chip (ridotta a pochi colori vs. piano alpha) sono scelte
// una volta sola all'avvio (si->settings.usealphacursor) e non cambiano mai
// a runtime - le funzioni sottostanti smistano internamente in base a quella
// flag.
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


// Riprogramma solo il registro di indirizzo superficie sulla slot indicata,
// senza toccare pixel/geometria - usato sia dal thread di animazione che
// dall'upload. In modalita' ridotta preserva il bit di enable/visibilita'
// gia' impostato (lo switch di frame non deve mai rendere visibile un
// cursore nascosto).
static void
sm750_set_cursor_frame_address(uint32 frameIndex)
{
	shared_info* si = gInfo->si;
	vuint32* regs = gInfo->regs;

	if (si->settings.usealphacursor) {
		uint32 offset = si->cursor.alphaFrameVramOffset[frameIndex] & 0x03FFFFF0;
		SM750_WREG32(SM750_DISP_PANEL_ALPHA_FB_ADDR, offset);
	} else {
		uint32 reg = si->card_info.is_panel ? SM750_DISP_PANEL_CUR_ADDR : SM750_DISP_CRT_CUR_ADDR;
		uint32 enableBit = SM750_REG32(reg) & (1u << 31);
		uint32 addr = si->cursor.frameVramOffset[frameIndex] & 0x03FFFFF0;
		SM750_WREG32(reg, enableBit | addr);
	}
}


static status_t
sm750_cursor_animation_thread(void*)
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

		sm750_set_cursor_frame_address(sCursorAnimation.currentFrame);
	}

	return B_OK;
}


static status_t
sm750_ensure_animation_thread(void)
{
	if (sCursorAnimation.thread >= B_OK)
		return B_OK;

	sCursorAnimation.semaphore = create_sem(0, "sm750 cursor ani");
	if (sCursorAnimation.semaphore < B_OK)
		return sCursorAnimation.semaphore;

	sCursorAnimation.terminate = false;
	sCursorAnimation.thread = spawn_thread(sm750_cursor_animation_thread,
		"sm750 cursor ani", B_DISPLAY_PRIORITY, NULL);
	if (sCursorAnimation.thread < B_OK) {
		delete_sem(sCursorAnimation.semaphore);
		sCursorAnimation.semaphore = -1;
		return sCursorAnimation.thread;
	}

	resume_thread(sCursorAnimation.thread);
	return B_OK;
}


// Identica alla sm750_update_alpha_cursor originale (stessa matematica di
// crop/allineamento empiricamente tarata - vedi i commenti italiani sotto),
// generalizzata solo per operare sulla slot "frameIndex" invece che sempre
// sull'unico buffer alpha originale.
static status_t
sm750_update_alpha_cursor_frame(uint32 frameIndex, uint16 x, uint16 y, uint16 width, uint16 height, uint16 bytesPerRow, const uint8* bitmapData)
{
	    // Dai vari test si nota che impostando right = left + 64 e 
        // bottom = top + 64 per coprire tutta l'area del cursore alpha
        // accade che l'ultima riga a destra e l'ultima in basso presentano
        // artefatti. Tagliando via le ultime righe l'artefatto scompare.
        // Ma l'artefatto si ripresenta quando muovo velocemente il mouse.
        // Da prove empiriche per un tradeoff accettabile l'area di 48x48
        // funziona abbastanza bene anche per lo spostamento veloce con 
        // move_cursor, a quella dimensione non si presentano troppo spesso
        // gli artefatti e l'area è sufficientemente grande per il trascinamento di icone
        // con il drag'n'drop.
    if (gInfo->alphacursor_frame_virtual_address[frameIndex] == NULL) {
        debug_printf("SM750_ACC: Cursor: alpha-cursor memory address not initialized\n");
        return B_NO_INIT;
    }

    shared_info *si = gInfo->si;
    vuint32 *regs = gInfo->regs;

    uint16* dest = (uint16*)gInfo->alphacursor_frame_virtual_address[frameIndex];
    const uint8* src = (const uint8* )bitmapData;

    // Posizione logica considerando l'hotspot (può diventare negativa)
    int16 left = (int16)x - (int16)si->cursor.hot_x;
    int16 top = (int16)y - (int16)si->cursor.hot_y;

    si->cursor.cursor_bitmap_width = width;
    si->cursor.cursor_bitmap_height = height;

    // --- CLIPPING SOFTWARE PER BORDI NEGATIVI ---
    int16 hw_left = left;
    int16 hw_top = top;
    
    int src_start_x = 0;
    int src_start_y = 0;
    
    int draw_width = width;
    int draw_height = height;

    // Se usciamo a sinistra
    if (hw_left < 0) {
        src_start_x = -hw_left;        // Quanti pixel saltare all'inizio della riga sorgente
        draw_width += hw_left;         // Riduciamo la larghezza da disegnare
        hw_left = 0;                   // La finestra hardware parte da 0 a schermo
    }

    // Se usciamo in alto
    if (hw_top < 0) {
        src_start_y = -hw_top;         // Quante righe saltare all'inizio
        draw_height += hw_top;         // Riduciamo l'altezza da disegnare
        hw_top = 0;                    // La finestra hardware parte da 0 a schermo
    }

    // Calcoliamo right e bottom basati sulla porzione effettivamente visibile
    int16 hw_right = hw_left + draw_width;
    int16 bottom = hw_top + draw_height;

    // Inviamo i valori positivi/clippati ai registri hardware dell'SM750
    uint32 tl_val = ((hw_top & 0x7FF) << 16) | (hw_left & 0x07FF);
    uint32 br_val = ((bottom & 0x7FF) << 16) | (hw_right & 0x07FF);

    SM750_WREG32(SM750_DISP_PANEL_ALPHA_PL_TL_POS, tl_val);
    SM750_WREG32(SM750_DISP_PANEL_ALPHA_PL_BR_POS, br_val);

    // Indirizzo VRAM allineato
    uint32 alpha_addr = si->cursor.alphaFrameVramOffset[frameIndex] & 0x03FFFFF0;
    SM750_WREG32(SM750_DISP_PANEL_ALPHA_FB_ADDR, alpha_addr);

    // Allineamento a 128-bit per il pitch dei blocchi basato sulla larghezza attiva
    uint32 raw_blocks = ((width * 2) + 15) / 16;
    uint32 aligned_blocks = (raw_blocks + 7) & ~7;
    if (aligned_blocks < 8) aligned_blocks = 8;
    uint32 reg_val = (aligned_blocks << 20) | (aligned_blocks << 4);

    SM750_WREG32(SM750_DISP_PANEL_ALPHA_FB_OFFSET_WWIDTH, reg_val);

    uint32 pitch_pixels = aligned_blocks * 8;

    // Pulisci l'area VRAM (64x64 pixel a 16-bit)
    memset(dest, 0, 64 * 64 * 2);

    // Copia dei pixel tenendo conto del ritaglio (cropping)
    if (draw_width > 0 && draw_height > 0) {
        for (int y_idx = 0; y_idx < draw_height && (src_start_y + y_idx) < height; y_idx++) {
            for (int x_idx = 0; x_idx < draw_width && (src_start_x + x_idx) < width; x_idx++) {
                
                int src_x = src_start_x + x_idx;
                int src_y = src_start_y + y_idx;

                const uint8* pixel = src + (src_y * bytesPerRow) + (src_x * 4);
                uint8 b = pixel[0] >> 4;
                uint8 g = pixel[1] >> 4;
                uint8 r = pixel[2] >> 4;
                uint8 a = pixel[3] >> 4;

                uint16 val = (a << 12) | (r << 8) | (g << 4) | b;
                
                // Scriviamo partendo da 0 nel buffer VRAM locale della finestra
                dest[y_idx * pitch_pixels + x_idx] = val;
            }
        }
    }

    uint32 alpha_ctrl = (1 << 2) | (3 << 0); // Enable = 1, Format = 11 (16-bit aRGB 4:4:4:4)
    SM750_WREG32(SM750_DISP_PANEL_ALPHA_CTRL, alpha_ctrl);

    return B_OK;
}
void
sm750_move_cursor(uint16 x, uint16 y)
{
    shared_info *si = gInfo->si;
    vuint32 *regs = gInfo->regs;

    int16 x_pos = (int16)x - (int16)si->cursor.hot_x;
    int16 y_pos = (int16)y - (int16)si->cursor.hot_y;

    uint32 reg_val = 0;

    if (x_pos < 0) {
        // Bit 11 = 1 (Out left), the value becomes positive (dinstance from border)
        reg_val |= (1 << 11) | (uint32)((-x_pos) & 0x3F); 
    } else {
        reg_val |= (uint32)(x_pos & 0x07FF);
    }

    if (y_pos < 0) {
        // Bit 27 = 1 (Out up)
        reg_val |= (1 << 27) | (uint32)(((-y_pos) & 0x3F) << 16);
    } else {
        reg_val |= (uint32)((y_pos & 0x07FF) << 16);
    }

    if (si->card_info.is_panel)
        SM750_WREG32(SM750_DISP_PANEL_CUR_POS, reg_val);
    else
        SM750_WREG32(SM750_DISP_CRT_CUR_POS, reg_val);
    
    if (si->settings.usealphacursor) {
    	//TODO: stabilire posizione ALPHA LAYER
    }

    si->cursor.x = x;
    si->cursor.y = y;
    
    if (si->settings.usealphacursor) {
    	// Valutiamo se la posizione attuale è fuori dai bordi (negativa)
        actualoutbounds = (x_pos < 0 || y_pos < 0);
        // Se ci troviamo fuori dai bordi O veniamo da una condizione di fuori bordo 
        // (serve a ripulire/ripristinare quando rientriamo o ci muoviamo sul confine)
        if (actualoutbounds || previousoutbounds) {
            // Richiamiamo l'helper che gestisce il posizionamento e il cropping software,
            // sul frame attualmente visualizzato (frame 0 se non e' in corso un'animazione).
            uint32 activeFrame = sCursorAnimation.running ? sCursorAnimation.currentFrame : 0;
            sm750_update_alpha_cursor_frame(activeFrame, x, y,
                                      si->cursor.frameWidth[activeFrame],
                                      si->cursor.frameHeight[activeFrame],
                                      si->cursor.frameBytesPerRow[activeFrame],
                                      si->cursor.frameBitmapData[activeFrame]);
        } else {
            // Caso standard in pieno schermo: aggiorniamo solo i registri della finestra 
            // senza ricalcolare la VRAM pixel per pixel (massima velocità)
            int16 left = x_pos;
            int16 top = y_pos;
            
            int16 width = (si->cursor.cursor_bitmap_width == 64) ? 63 : si->cursor.cursor_bitmap_width;
            int16 height = (si->cursor.cursor_bitmap_height == 64) ? 63 : si->cursor.cursor_bitmap_height;
            int16 right = left + width;
            int16 bottom = top + height;

            uint32 tl_val = ((top & 0x7FF) << 16) | (left & 0x07FF);
            uint32 br_val = ((bottom & 0x7FF) << 16) | (right & 0x07FF);

            SM750_WREG32(SM750_DISP_PANEL_ALPHA_PL_TL_POS, tl_val);
            SM750_WREG32(SM750_DISP_PANEL_ALPHA_PL_BR_POS, br_val);
        }// Aggiorniamo lo stato precedente per il prossimo movimento
        previousoutbounds = actualoutbounds;
    }
}

void
sm750_show_cursor(bool is_visible)
{
    shared_info *si = gInfo->si;
    vuint32 *regs = gInfo->regs;
    // Cursor Register
    uint32 reg = si->card_info.is_panel ? SM750_DISP_PANEL_CUR_ADDR : SM750_DISP_CRT_CUR_ADDR;
    uint32 ctrl = SM750_REG32(reg);
    if (is_visible)
        ctrl |= (1 << 31); // Enable bit
    else
        ctrl &= ~(1 << 31);
    SM750_WREG32(reg, ctrl);
    // Alpha layer
    if (si->settings.usealphacursor) {
        ctrl = SM750_REG32(SM750_DISP_PANEL_ALPHA_CTRL);
        if (is_visible)
            ctrl |= (1 << 2); // Enable bit
        else
            ctrl &= ~(1 << 2);
        SM750_WREG32(SM750_DISP_PANEL_ALPHA_CTRL, ctrl);
    }
}

status_t
sm750_set_cursor_shape(uint16 width, uint16 height, uint16 hotX, uint16 hotY,
    const uint8* andMask, const uint8* xorMask)
{
	//CALLED();
    if (width > 64 || height > 64)
        return B_BAD_VALUE;

    shared_info *si = gInfo->si;
    vuint32 *regs = gInfo->regs;
    
    //uint8* dest = (uint8*)si->cursor.v_address;
    uint8* dest = (uint8*)gInfo->cursor_virtual_address;

    if (dest == NULL)
        return B_NO_INIT;

    si->cursor.hot_x = hotX;
    si->cursor.hot_y = hotY;

    // 1. Clear: 0x00 is transparent
    memset(dest, 0x00, 1024);

    // 2. BeOS mask traduction -> SM750 2-bit Format
    // Every hardware row is 16 byte (64 pixels * 2 bit)
    uint32 byteWidth = (width + 7) / 8; // Width in byte of the original mask

    for (uint32 y = 0; y < height; y++) {
        for (uint32 x = 0; x < width; x++) {
            // bit extraction of Haiku's masks
            uint32 srcByteIdx = (y * byteWidth) + (x / 8);
            uint8 bitPos = 7 - (x % 8);
            
            bool andBit = (andMask[srcByteIdx] >> bitPos) & 0x01;
            bool xorBit = (xorMask[srcByteIdx] >> bitPos) & 0x01;

            // AND=0, XOR=0 -> Black (10 binary = 2)
            // AND=0, XOR=1 -> White (01 binary = 1)
            // AND=1, XOR=0 -> Transparent (00 binary = 0)
            // AND=1, XOR=1 -> Inversion/Black (for now let's make it black = 2)
            
            uint8 val = 0;
            if (andBit == 0) {
                if (xorBit == 0) val = 2; // Black
                else val = 1;            // White
            } else {
                if (xorBit == 1) val = 2; // Inversion, mapped to black
                else val = 0;            // Transparent
            }

            // writing to the interlaced buffer (16 bytes per row)
            uint32 destByteIdx = (y * 16) + (x / 4);
            uint8 shift = (x % 4) * 2;
            dest[destByteIdx] |= (val << shift);
        }
    }

    uint32 addr_val = (1 << 31) | (si->cursor.vram_offset & 0x03FFFFF0);
    uint32 color12 = 0x0000FFFF;

    if (si->card_info.is_panel) {
        SM750_WREG32(SM750_DISP_PANEL_CUR_COLOR12, color12);
        SM750_WREG32(SM750_DISP_PANEL_CUR_ADDR, addr_val);
    } else {
        SM750_WREG32(SM750_DISP_CRT_CUR_COLOR12, color12);
        SM750_WREG32(SM750_DISP_CRT_CUR_ADDR, addr_val);
    }

    sm750_move_cursor(si->cursor.x, si->cursor.y);

    return B_OK;
}

status_t
sm750_set_cursor_bitmap(uint16 width, uint16 height, uint16 hotX, uint16 hotY,
    color_space colorSpace, uint16 bytesPerRow, const uint8* bitmapData)
{
	//CALLED();
	(void)colorSpace;
	// TODO: if the mouse flickers bit 31 of SM750_DISP_PANEL_CUR_ADDR should be set 0
	//       while changing the bitmap
    shared_info *si = gInfo->si;
    vuint32 *regs = gInfo->regs;
    si->cursor.hot_x = hotX;
    si->cursor.hot_y = hotY;
    si->cursor.cursor_bitmap_width = width;
    si->cursor.cursor_bitmap_height = height;
    si->cursor.bytesPerRaw = bytesPerRow;
    si->cursor.bitmapData = bitmapData;

    if (sm750_ensure_animation_thread() != B_OK)
        return B_ERROR;

    uint32 frameCount = si->settings.usealphacursor
        ? si->cursor.alphaFrameCount : si->cursor.frameCount;

    // Selezione della slot di destinazione per l'animazione: stesso schema
    // gia' usato per kyro/radeon_hd/ATI/Matrox/S3/Trident/XGI.
    if (!sCursorAnimation.running) {
        if (sCursorAnimation.nextUploadFrame == 0)
            sCursorAnimation.uploadedFrameCount = 0;
        sCursorAnimation.currentFrame = sCursorAnimation.nextUploadFrame;
    }

    uint32 targetFrame = sCursorAnimation.running
        ? sCursorAnimation.currentFrame : sCursorAnimation.nextUploadFrame;
    if (targetFrame >= frameCount)
        targetFrame = 0;

    // Ricordiamo la bitmap grezza di questo frame: serve all'helper di
    // ridisegno del piano alpha vicino ai bordi schermo (vedi
    // sm750_move_cursor), che deve poter ridisegnare QUALSIASI frame, non
    // solo l'ultimo caricato.
    si->cursor.frameWidth[targetFrame] = width;
    si->cursor.frameHeight[targetFrame] = height;
    si->cursor.frameBytesPerRow[targetFrame] = bytesPerRow;
    si->cursor.frameBitmapData[targetFrame] = bitmapData;

    status_t result;

    if (si->settings.usealphacursor){
    	// Calcoliamo la posizione top-left considerando l'hotspot
        int16 left = (int16)si->cursor.x - (int16)si->cursor.hot_x;
        int16 top = (int16)si->cursor.y - (int16)si->cursor.hot_y;

        // --- GESTIONE STATO BOUNDS ---
        actualoutbounds = (left < 0 || top < 0);
        previousoutbounds = actualoutbounds; // Allineiamo lo stato iniziale
        // Sfruttiamo l'helper centralizzato che gestisce anche i bordi negativi e il cropping
        result = sm750_update_alpha_cursor_frame(targetFrame, si->cursor.x, si->cursor.y,
            width, height, bytesPerRow, bitmapData);
    } else {
        //if (si->cursor.v_address == NULL) {
        if (gInfo->cursor_frame_virtual_address[targetFrame] == NULL) {
            debug_printf("SM750_ACC: Cursor: cursor memory address not initialized\n");
            return B_NO_INIT;
        }

        uint8* dest = (uint8*)gInfo->cursor_frame_virtual_address[targetFrame];

        // Cleaning the entire KB
        // 2-bit per pixel format
        // Fill with "10" (Transparent) -> binary: 10101010 = 0xAA
        // with 0xFF Black
        // with 0xAA BLACK
        // with 0x55 White
        // with 0x00 TRANSPARENTE
        memset(dest, 0x00, 1024);  // transparence

        const uint8* src = (const uint8*)bitmapData;

        for (uint32 y = 0; y < height && y < 64; y++) {
            for (uint32 x = 0; x < width && x < 64; x++) {
                const uint8* pixel = src + (y * bytesPerRow) + (x * 4);
                uint8 a = pixel[3]; // Alpha
                uint8 r = pixel[2];
                uint8 g = pixel[1];
                uint8 b = pixel[0];

                uint8 val = 0; // Default: 00 (Transparent)

                /* two colors white and black
                if (a > 128) {
                    // Luminance for select between White and Black
                    uint32 luma = (r + g + b) / 3;
                    if (luma > 128)
                        val = 1; // 01 (Color 1: WHITE)
                    else
                        val = 2; // 10 (Color 2: BLACK)
                }*/
                if (a < 100) {
                    val = 0; // Transparent
                } else if (a < 200) {
                    val = 3; // COLOR 3 (Grey for shadows!)
                } else {
                    // Solid pixel: White or black
                    uint32 luma = (r + g + b) / 3;
                    val = (luma > 128) ? 1 : 2; 
                }

                // Insert the 2 bits
                uint32 byteIdx = (y * 16) + (x / 4); 
                uint8 shift = (x % 4) * 2;
            
                dest[byteIdx] |= (val << shift);
            }
        }
    
    
        // Force colors and address
        uint32 addr_val = (1 << 31) | (si->cursor.frameVramOffset[targetFrame] & 0x03FFFFF0);

        // SM750 Color registers
        // Color 1 (0,0) = White
        // Color 2 (0,1) = Black
        uint32 color12 = 0x0000FFFF; // Black higher bits, White lower bits
        uint32 color3 = 0x00888888; // Grey (R=88, G=88, B=88)

        if (si->card_info.is_panel) {
            SM750_WREG32(SM750_DISP_PANEL_CUR_COLOR12, color12);
            SM750_WREG32(SM750_DISP_PANEL_CUR_COLOR3, color3);
            SM750_WREG32(SM750_DISP_PANEL_CUR_ADDR, addr_val);
        } else {
            SM750_WREG32(SM750_DISP_CRT_CUR_COLOR12, color12);
            SM750_WREG32(SM750_DISP_CRT_CUR_COLOR3, color3);
            SM750_WREG32(SM750_DISP_CRT_CUR_ADDR, addr_val);
        }

        result = B_OK;
    }

    if (result == B_OK && !sCursorAnimation.running) {
        if (targetFrame + 1 < frameCount)
            sCursorAnimation.nextUploadFrame = targetFrame + 1;
        else
            sCursorAnimation.nextUploadFrame = 0;

        if (sCursorAnimation.uploadedFrameCount < targetFrame + 1)
            sCursorAnimation.uploadedFrameCount = targetFrame + 1;
    }

    return result;
}


uint32
sm750_get_cursor_bits(void)
{
	if (gInfo == NULL || gInfo->si == NULL)
		return 0;

	if (gInfo->si->settings.usealphacursor) return 32;
	return gInfo->si->settings.cursorbits;
}


bool
sm750_supports_cursor_ani(void)
{
	if (gInfo == NULL || gInfo->si == NULL)
		return false;

	shared_info* si = gInfo->si;
	uint32 frameCount = si->settings.usealphacursor
		? si->cursor.alphaFrameCount : si->cursor.frameCount;
	return frameCount >= 2;
}


uint32
sm750_cursor_supported_frames(void)
{
	if (!sm750_supports_cursor_ani())
		return 0;

	shared_info* si = gInfo->si;
	uint32 frameCount = si->settings.usealphacursor
		? si->cursor.alphaFrameCount : si->cursor.frameCount;
	return frameCount - 1;
}


status_t
sm750_start_cursor_ani(void)
{
	if (!sm750_supports_cursor_ani())
		return B_NOT_SUPPORTED;
	if (sCursorAnimation.uploadedFrameCount < 2)
		return B_BAD_VALUE;
	if (sm750_ensure_animation_thread() != B_OK)
		return B_ERROR;

	sCursorAnimation.currentFrame = 0;
	sm750_set_cursor_frame_address(0);

	sCursorAnimation.running = true;
	if (sCursorAnimation.semaphore >= B_OK)
		release_sem(sCursorAnimation.semaphore);
	return B_OK;
}


status_t
sm750_stop_cursor_ani(void)
{
	if (!sm750_supports_cursor_ani())
		return B_NOT_SUPPORTED;

	sCursorAnimation.running = false;
	sCursorAnimation.currentFrame = 0;
	sCursorAnimation.nextUploadFrame = 0;

	sm750_set_cursor_frame_address(0);
	return B_OK;
}


status_t
sm750_set_cursor_ani_speed(float speedFactor)
{
	if (!sm750_supports_cursor_ani())
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
sm750_set_cursor_ani_start_delay(uint32 startDelayMs)
{
	if (!sm750_supports_cursor_ani())
		return B_NOT_SUPPORTED;
	sCursorAnimation.startDelay = (bigtime_t)startDelayMs * 1000;
	return B_OK;
}


void
sm750_cursor_ani_uninit(void)
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
