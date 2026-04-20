/*
 * Copyright 2018, Your Name <your@email.address>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include <string.h>
#include "sm750_macros.h"
#include "DriverInterface.h"
#include "protos.h"


#define CALLED() debug_printf("SM750_ACC: CALLED %s\n", __FUNCTION__)

extern accelerant_info *gInfo;

void
sm750_move_cursor(uint16 x, uint16 y)
{
    shared_info *si = gInfo->si;
    vuint32 *regs = gInfo->regs;

    int16 x_pos = (int16)x - (int16)si->cursor.hot_x;
    int16 y_pos = (int16)y - (int16)si->cursor.hot_y;

    uint32 reg_val = 0;

    // Gestione X (Orizzontale)
    if (x_pos < 0) {
        // Bit 11 = 1 (Fuori a sinistra), il valore diventa positivo (distanza dal bordo)
        reg_val |= (1 << 11) | (uint32)((-x_pos) & 0x3F); 
    } else {
        reg_val |= (uint32)(x_pos & 0x07FF);
    }

    // Gestione Y (Verticale)
    if (y_pos < 0) {
        // Bit 27 = 1 (Fuori in alto)
        reg_val |= (1 << 27) | (uint32)(((-y_pos) & 0x3F) << 16);
    } else {
        reg_val |= (uint32)((y_pos & 0x07FF) << 16);
    }

    if (si->card_info.is_panel)
        SM750_WREG32(SM750_DISP_PANEL_CUR_POS, reg_val);
    else
        SM750_WREG32(SM750_DISP_CRT_CUR_POS, reg_val);

    si->cursor.x = x;
    si->cursor.y = y;
}

/*
void
sm750_move_cursor_alpha(uint16 x, uint16 y)
{
    shared_info *si = gInfo->si;

    // Calcolo posizione effettiva (con segno per gestire l'uscita dallo schermo)
    int16 x_pos = (int16)x - (int16)si->cursor.hot_x;
    int16 y_pos = (int16)y - (int16)si->cursor.hot_y;
    
    si->cursor.x = x;
    si->cursor.y = y;

    if (si->card_info.is_panel) {
        // 1. Spostamento Cursore Standard (Legacy)
        // Usiamo la tua maschera 0x07FF per sicurezza sui bit 10:0 e 26:16
        SM750_WREG32(SM750_DISP_PANEL_CUR_POS, ((uint32)(x_pos & 0x07FF) << 16) | (y_pos & 0x07FF));

        // 2. Spostamento Finestra Alpha (Fondamentale per il cursore moderno)
        // Top-Left
        uint32 tl = ((uint32)(y_pos & 0x07FF) << 16) | (x_pos & 0x07FF);
        // Bottom-Right (assumiamo 64x64)
        uint32 br = ((uint32)((y_pos + 64) & 0x07FF) << 16) | ((x_pos + 64) & 0x07FF);

        SM750_WREG32(SM750_DISP_PANEL_ALPHA_PL_TL_POS, tl);
        SM750_WREG32(SM750_DISP_PANEL_ALPHA_PL_BR_POS, br);
    } else {
        // Solo CRT (Legacy)
        SM750_WREG32(SM750_DISP_CRT_CUR_POS, ((uint32)(x_pos & 0x07FF) << 16) | (y_pos & 0x07FF));
    }
}*/

void
sm750_show_cursor(bool is_visible)
{
    shared_info *si = gInfo->si;
    vuint32 *regs = gInfo->regs;
    
    uint32 reg = si->card_info.is_panel ? SM750_DISP_PANEL_CUR_ADDR : SM750_DISP_CRT_CUR_ADDR;
    
    uint32 ctrl = SM750_REG32(reg);
    
    if (is_visible)
        ctrl |= (1 << 31); // Enable bit
    else
        ctrl &= ~(1 << 31);
        
    SM750_WREG32(reg, ctrl);
}
/* vecchia
status_t
sm750_set_cursor_shape(uint16 width, uint16 height, uint16 hot_x, uint16 hot_y,
    const uint8 *and_mask, const uint8 *xor_mask)
{
    shared_info *si = gInfo->si;
    vuint32 *regs = gInfo->regs;
    
    // 1. Destinazione in VRAM (ultimi 2KB)
    uint32 cursorOffset = si->card_info.mem_size - 2048;
    uint8 *dest = (uint8 *)si->framebuffer + cursorOffset;

    memset(dest, 0, 1024);

    // 2. Bit-packing (rimane invariato, è il formato hardware SMI)
    for (int y = 0; y < height && y < 64; y++) {
        uint8 *line_dest = dest + (y * 16); 
        for (int x = 0; x < width && x < 64; x++) {
            int byte_idx = (y * ((width + 7) / 8)) + (x / 8);
            int bit_idx = 7 - (x % 8);
            
            bool a = (and_mask[byte_idx] >> bit_idx) & 1;
            bool x_m = (xor_mask[byte_idx] >> bit_idx) & 1;

            if (!a) { 
                uint8 val = x_m ? 0x03 : 0x02; 
                line_dest[x / 4] |= (val << (2 * (3 - (x % 4))));
            }
        }
    }

    // 3. Update Registri in base all'uscita attiva
    if (si->card_info.is_panel) {
        SM750_WREG32(SM750_DISP_PANEL_CUR_ADDR, (cursorOffset & 0x03FFFFFF) | (1 << 31));
        SM750_WREG32(SM750_DISP_PANEL_CUR_COLOR12, (0xFFFF << 16) | 0x0000);
        SM750_WREG32(SM750_DISP_PANEL_CUR_COLOR3, 0x0000FFFF);
    } else {
        SM750_WREG32(SM750_DISP_CRT_CUR_ADDR, (cursorOffset & 0x03FFFFFF) | (1 << 31));
        SM750_WREG32(SM750_DISP_CRT_CUR_COLOR12, (0xFFFF << 16) | 0x0000);
        SM750_WREG32(SM750_DISP_CRT_CUR_COLOR3, 0x0000FFFF);
    }

    si->cursor.hot_x = hot_x;
    si->cursor.hot_y = hot_y;
    return B_OK;
}*/

status_t
sm750_set_cursor_shape(uint16 width, uint16 height, uint16 hotX, uint16 hotY,
    const uint8* andMask, const uint8* xorMask)
{
	CALLED();
    // L'hardware SM750 supporta fino a 64x64. BeOS/Haiku mandano solitamente 16x16.
    if (width > 64 || height > 64)
        return B_BAD_VALUE;

    shared_info *si = gInfo->si;
    vuint32 *regs = gInfo->regs;
    uint8* dest = (uint8*)si->cursor.v_address;

    if (dest == NULL)
        return B_NO_INIT;

    si->cursor.hot_x = hotX;
    si->cursor.hot_y = hotY;

    // 1. Inizializzazione: Tutto trasparente
    // AND = 0xFF (Trasparente), XOR = 0x00
    memset(dest, 0xff, 512);
    memset(dest + 512, 0x00, 512);

    // 2. Copia delle maschere
    // Haiku passa le maschere compatte. Dobbiamo copiarle riga per riga 
    // perché la nostra riga hardware è sempre di 8 byte (64 pixel).
    uint32 byteWidth = (width + 7) / 8;

    for (uint32 y = 0; y < height; y++) {
        // Copia riga AND nella prima metà del buffer (offset 0)
        memcpy(dest + (y * 8), andMask + (y * byteWidth), byteWidth);
        
        // Copia riga XOR nella seconda metà del buffer (offset 512)
        memcpy(dest + 512 + (y * 8), xorMask + (y * byteWidth), byteWidth);
    }

    // 3. Attivazione Hardware (come abbiamo già fatto)
    uint32 addr_val = (1 << 31) | (CRT_CURSOR_VRAM_OFFSET & 0x03FFFFF0);

    if (si->card_info.is_panel) {
        SM750_WREG32(SM750_DISP_PANEL_CUR_COLOR12, 0x0000FFFF); // Nero / Bianco
        SM750_WREG32(SM750_DISP_PANEL_CUR_COLOR3, 0x0000FFFF);
        SM750_WREG32(SM750_DISP_PANEL_CUR_ADDR, addr_val);
    } else {
        SM750_WREG32(SM750_DISP_CRT_CUR_COLOR12, 0x0000FFFF);
        SM750_WREG32(SM750_DISP_CRT_CUR_COLOR3, 0x0000FFFF);
        SM750_WREG32(SM750_DISP_CRT_CUR_ADDR, addr_val);
    }

    // Spostiamo il cursore perché cambiando l'hotspot la posizione deve essere aggiornata
    sm750_move_cursor(si->cursor.x, si->cursor.y);

    return B_OK;
}

// macro per gestire colore cursore: Color16=((R>>3)<<11)|((G>>2)<<5)|(B>>3)

status_t
sm750_set_cursor_bitmap(uint16 width, uint16 height, uint16 hotX, uint16 hotY,
    color_space colorSpace, uint16 bytesPerRow, const uint8* bitmapData)
{
	CALLED();
    shared_info *si = gInfo->si;
    vuint32 *regs = gInfo->regs;

    if (si->cursor.v_address == NULL) {
    	debug_printf("SM750_ACC: Cursor: indirizzo di memoria non inizializzato");
        return B_NO_INIT;
    }

    si->cursor.hot_x = hotX;
    si->cursor.hot_y = hotY;
    si->cursor.width = width;
    si->cursor.height = height;

    // Puntatore alla VRAM riservata (1KB)
    uint8* dest = (uint8*)si->cursor.v_address;
    
    // 1. Pulizia: AND=1 (Trasparente), XOR=0
    memset(dest, 0xff, 512); 
    memset(dest + 512, 0x00, 512);
    /*
    // 2. Conversione da RGBA32 a 2-bit (AND/XOR)
    if (colorSpace == B_RGBA32 || colorSpace == B_RGB32) {
        const uint32* src = (const uint32*)bitmapData;
        for (uint16 y = 0; y < height && y < 64; y++) {
            for (uint16 x = 0; x < width && x < 64; x++) {
                uint32 pixel = src[y * (bytesPerRow / 4) + x];
                uint8 alpha = (pixel >> 24) & 0xff;
                uint8 luma = (pixel >> 16) & 0xff; // R come riferimento
                
                uint8 bitPos = 7 - (x % 8);
                uint32 byteIdx = y * 8 + (x / 8);
                
                if (alpha > 128) {
                    // Opaco: bit AND a 0
                    dest[byteIdx] &= ~(1 << bitPos);
                    
                    if (luma > 128) {
                        // Bianco: bit XOR a 1
                        dest[512 + byteIdx] |= (1 << bitPos);
                    }
                }
            }
        }
    }*/
    const uint32* src = (const uint32*)bitmapData;
    uint32 bPerRow = bytesPerRow / 4; // larghezza sorgente in pixel

    for (uint16 y = 0; y < height && y < 64; y++) {
        for (uint16 x = 0; x < width && x < 64; x++) {
            uint32 pixel = src[y * bPerRow + x];
            uint8 a = (pixel >> 24) & 0xff;
            uint8 r = (pixel >> 16) & 0xff;
            uint8 g = (pixel >> 8) & 0xff;
            uint8 b = pixel & 0xff;

            uint8 luma = (uint8)(0.299f * r + 0.587f * g + 0.114f * b);
            uint32 byteIdx = y * 8 + (x / 8);
            uint8 bitPos = 7 - (x % 8);

            if (a < 128) {
                // TRASPARENTE: AND=1, XOR=0
                dest[byteIdx] |= (1 << bitPos);
                dest[512 + byteIdx] &= ~(1 << bitPos);
            } else if (luma > 128) {
                // BIANCO: AND=0, XOR=1
                dest[byteIdx] &= ~(1 << bitPos);
                dest[512 + byteIdx] |= (1 << bitPos);
            } else {
                // NERO: AND=0, XOR=0
                dest[byteIdx] &= ~(1 << bitPos);
                dest[512 + byteIdx] &= ~(1 << bitPos);
            }
        }
    }

    // 3. Preparazione valore registro Indirizzo
    // Bit 31: Enable, Bit 27: Local Memory (0), Bit 25:4: Address with 3:0 hardwired 0
    uint32 addr_val = (1 << 31) | (CRT_CURSOR_VRAM_OFFSET & 0x03FFFFF0);
    
    if (si->card_info.is_panel) {
        // PANEL (Primary)
        SM750_WREG32(SM750_DISP_PANEL_CUR_COLOR12, 0x0000FFFF); 
        SM750_WREG32(SM750_DISP_PANEL_CUR_COLOR3, 0x0000F800); //0x0000FFFF
        SM750_WREG32(SM750_DISP_PANEL_CUR_ADDR, addr_val);
    } else {
        // CRT (Secondary)
        SM750_WREG32(SM750_DISP_CRT_CUR_COLOR12, 0x0000FFFF);
        SM750_WREG32(SM750_DISP_CRT_CUR_COLOR3, 0x0000F800); //0x0000FFFF
        SM750_WREG32(SM750_DISP_CRT_CUR_ADDR, addr_val);
    }

    return B_OK;
}
