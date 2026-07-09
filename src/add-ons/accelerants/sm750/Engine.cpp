/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include <Accelerant.h>
#include <Debug.h>
#include <SupportDefs.h>
#include <StorageDefs.h> //per B_PATH_NAME_LENGTH
#include <string.h>
#include <errno.h>

#include "DriverInterface.h"
#include "sm750_macros.h"
#include "protos.h"
//#include "benaphore.h"

#define CALLED() debug_printf("SM750_ACC: %s\n", __FUNCTION__)
// Per il registro 2D Destination e Dimension: Impacchetta X e Y rispettando il bit 31 (Wrap) e 30-29 (Res)
// #define PACK_XY(x, y) ((((uint32)(x) & 0x1FFF) << 16) | ((uint32)(y) & 0xFFFF))

extern accelerant_info *gInfo;

static status_t 
sm750_calc_pll(uint32 target_khz, uint32* out_M, uint32* out_N, uint32* out_DIV)
{
    uint32 M, N, DIV_idx;
    uint32 best_M = 0, best_N = 0, best_DIV = 1;
    
    // Usiamo il f_ref salvato (14.31818)
    float f_ref = gInfo->si->card_info.f_ref; 
    float target = (float)target_khz / 1000.0f; // MHz
    float min_diff = 1000.0f;

    // Divisori totali possibili (combinazioni di OD e POD)
    uint32 possible_divisors[] = {1, 2, 4, 8, 16, 32, 64};
    
    for (DIV_idx = 0; DIV_idx < 7; DIV_idx++) {
        uint32 div = possible_divisors[DIV_idx];
        
        for (N = 2; N <= 15; N++) {
            // M = (Target * Div * N) / F_ref
            M = (uint32)((target * (float)div * (float)N) / f_ref + 0.5f);
            
            if (M < 2 || M > 255) continue;

            // VCO = F_ref * M / N
            // Il range del VCO per SM750 è tipicamente 240-480 MHz
            float vco = f_ref * ((float)M / (float)N);
            if (vco < 240.0f || vco > 480.0f) continue;

            float actual = vco / (float)div;
            float diff = (actual > target) ? (actual - target) : (target - actual);

            if (diff < min_diff) {
                min_diff = diff;
                best_M = M;
                best_N = N;
                best_DIV = div; // Restituiamo il divisore reale (es. 4, 8, 16...)
            }
            
            if (diff < 0.001f) goto found; // Quasi perfetto
        }
    }

found:
    if (best_M == 0) return B_ERROR;

    *out_M = best_M;
    *out_N = best_N;
    *out_DIV = best_DIV; 
    
    return B_OK;
}

void sm750_program_pll(uint32 target_khz, bool is_panel)
{
	vuint32 *regs = gInfo->regs;
    uint32 m, n, div_val;
    
    // Cerchiamo i valori ottimali. 
    // m (1-255), n (2-15), div_val (1, 2, 4, 8, 16, 32, 64)
    if (sm750_calc_pll(target_khz, &m, &n, &div_val) != B_OK) return;
    //debug_printf("SM750_PLL: target kH %u, m %u, n %u\n",target_khz,m,n);

    uint32 pod_bits = 0;
    uint32 od_bits = 0;

    // Mappatura logica del divisore totale ai campi POD e OD
    if (div_val <= 8) {
        pod_bits = 0; // Dividi per 1
        // Mappatura OD: 1->0, 2->1, 4->2, 8->3
        if (div_val == 2) od_bits = 1; // dividi per 2
        else if (div_val == 4) od_bits = 2; // dividi per 4
        else if (div_val == 8) od_bits = 3; // dividi per 8
    } else {
        // Per divisori > 8, usiamo OD al massimo (/8) e aumentiamo POD
        od_bits = 3; // base di divisione: 8
        if (div_val == 16) pod_bits = 1;      // 2 * 8
        else if (div_val == 32) pod_bits = 2; // 4 * 8
        else if (div_val == 64) pod_bits = 3; // 8 * 8
    }

    uint32 pll_reg = 0;
    //debug_printf("SM750_PLL: pod_bits %u, od_bits %u, n %u, m %u\n",pod_bits,od_bits,n,m);
    pll_reg |= (1 << 17);               // PD: PLL Power On
    pll_reg |= (pod_bits << 14);        // POD
    pll_reg |= (od_bits << 12);         // OD
    pll_reg |= ((n & 0x0F) << 8);       // N (bit 11:8)
    pll_reg |= (m & 0xFF);              // M (bit 7:0)
    
    // impostiamo entrambi i pll alla stessa frequenza giusto per vedere se cambia qualcosa
    SM750_WREG32(SM750_DISP_PANEL_PLL,pll_reg);
    SM750_WREG32(SM750_DISP_CRT_PLL,pll_reg);
    //commentare  se non va bene... // VERIFICA CON FUNZIONAMENTO OVERLAY
    uint32 ctrl_offset = is_panel ? SM750_CRT_CONTROL : SM750_PANEL_CONTROL;
    pll_reg &= ~(1 << 17); // PLL Power down
    SM750_WREG32(ctrl_offset,pll_reg); // disattiva il pll in ctr o in panel se non usato
    snooze(1500); 
    //debug_printf("SM750_PLL: Programmazione %s a %u kHz\n", is_panel ? "PANEL" : "CRT", target_khz);
    //debug_printf("SM750_PLL: M:%u N:%u Div:%u -> Reg 0x%08x\n", m, n, div_val, pll_reg);
}

status_t sm750_move_display_area(uint16 h_display_start, uint16 v_display_start)
{
	CALLED();
    shared_info *si = gInfo->si;
    
    uint32 bytes_per_pixel = (si->dm.space == B_RGB32) ? 4 : 2;
    
    // L'offset è (Y * LarghezzaVirtuale + X) * Bpp
    uint32 start_addr = (v_display_start * si->dm.virtual_width + h_display_start) * bytes_per_pixel;

    // Usiamo l'helper che gestisce allineamento e bit riservati
    return sm750_set_fb_addr(start_addr, si->card_info.is_panel);
}




/* Limiti del Pixel Clock (Richiesto!) */
status_t sm750_get_pixel_clock_limits(display_mode *dm, uint32 *low, uint32 *high) {
    *low = 10000;   // 10 MHz
    *high = 300000; // 300 MHz
    return B_OK;
}


/* *************************** */
/*      Engine Management      */
/* *************************** */


//static engine_token sm750_engine_token = { 1, 0, NULL };

uint32 sm750_accelerant_engine_count(void) {
	CALLED();
    return 1;
}

status_t sm750_acquire_engine(uint32 capabilities, uint32 max_priority, 
                               sync_token *st, engine_token **et) {
    CALLED();
    // 1. Acquisizione ultra-veloce
    if (gInfo->si->engine.lock.Acquire() != B_OK)
		return B_ERROR;
    //AQUIRE_BEN(gInfo->si->engine.lock);

    // 2. Sincronizzazione con lavori precedenti se necessario
    if (st) sm750_sync_to_token(st);

    // 3. Restituiamo il nostro token statico
    *et = &(gInfo->sm750_engine_token);
    return B_OK;
}

void sm750_release_engine(engine_token *et, sync_token *st) {
    CALLED();
    // Se l'App Server ha chiesto un token per il lavoro appena finito, lo generiamo
    if (st) sm750_get_sync_token(et, st);
    
    // Rilasciamo il motore
    //RELEASE_BEN(gInfo->si->engine.lock);
    gInfo->si->engine.lock.Release();
}

void sm750_wait_engine_idle(void) {
    CALLED();
    vuint32 *regs = gInfo->regs;
    // Bit 31: 0 = Idle, 1 = Busy
    //while (SM750_REG32(SM750_2D_CONTROL) & (1U << 31)) {
    //    asm volatile ("pause");
    //}
    // usiamo il registro di livello più alto
    // 1. Aspetta che il motore 2D smetta di elaborare nuovi comandi
    // Bit 22: 1 = Busy, 0 = Idle
    while (SM750_REG32(SM750_SYS_CTRL) & (1U << 22)) {
        asm volatile ("pause");
    }
    // 2. Aspetta che la FIFO di memoria sia completamente vuota
    // Bit 21: 0 = Not Empty, 1 = Empty
    while (!(SM750_REG32(SM750_SYS_CTRL) & (1U << 21))) {
        asm volatile ("pause");
    }
}
status_t sm750_get_sync_token(engine_token *et, sync_token *st) {
    CALLED();
    // Il sync_token serve a Haiku per sapere a che punto è il lavoro.
    // Per ora usiamo un contatore semplice: ogni volta che rilasciamo il motore,
    // diciamo che il "tempo" è andato avanti.
    st->engine_id = et->engine_id;
    st->counter = gInfo->si->engine.count++; // Usiamo fifo_slots come contatore generico
    return B_OK;
}

status_t sm750_sync_to_token(sync_token *st) {
	CALLED();
	// TODO
    // Quando Haiku ci passa un token e dice "Sincronizzati", 
    // l'unico modo sicuro che abbiamo (per ora) è aspettare che l'engine sia IDLE.
    sm750_wait_engine_idle();
    return B_OK;
}
/*
void sm750_init_2d_engine(uint32 width, uint32 space) {
    vuint32 *regs = gInfo->regs;

    // 1. Aspetta che l'engine sia calmo
    sm750_wait_engine_idle();

    // 2. Imposta il PITCH (0x100010)
    // Entrambi source e destination usano la larghezza della risoluzione attuale
    uint32 pitch = ((width & 0x1FFF) << 16) | (width & 0x1FFF);
    SM750_WREG32(0x100010, pitch);

    // 3. Imposta SOURCE e DESTINATION BASE (Indirizzi di partenza in VRAM)
    // Di solito 0 per entrambi se usiamo un unico framebuffer lineare
    SM750_WREG32(0x100040, 0); // 2D Source Base offset
    SM750_WREG32(0x100044, 0); // 2D Destination Base offset

    // 4. Imposta STRETCH & FORMAT (0x10001C)
    uint32 fmt = 0;
    if (space == B_RGB32) fmt |= (2 << 20); // 32 bpp
    else if (space == B_RGB16) fmt |= (1 << 20); // 16 bpp
    
    fmt |= (0 << 16); // XY Addressing Mode (0000)
    SM750_WREG32(0x10001C, fmt);
    
    // 5. Imposta i CLIP (Massima area disegnabile)
    // Clip Top-Left (0,0)
    SM750_WREG32(0x10002C, 0); 
    // Clip Bottom-Right (Max Width, Max Height) - Offset 0x100030?
    // SM750_WREG32(0x100030, (height << 16) | width);
}*/

void sm750_init_2d_engine(display_mode *mode) {
	CALLED();
    vuint32 *regs = gInfo->regs;
    // USIAMO LA LARGHEZZA VIRTUALE (fondamentale per l'allineamento VRAM)
    uint32 width = mode->virtual_width; 
    uint32 height = mode->virtual_height;
    uint32 bpp_code;

    // Determiniamo il codice colore per il registro 0x10001C
    switch (mode->space) {
        case B_RGB32: bpp_code = 2; break; // 32-bpp
        case B_RGB16: bpp_code = 1; break; // 16-bpp
        default:      bpp_code = 0; break; // 8-bpp
    }

    // 1. Aspetta che l'engine sia libero
    sm750_wait_engine_idle();

    // 2. Imposta il Pitch (0x100010)
    // 1. Pitch (0x100010): X dimension in pixels
    // Bit 28:16 Destination, Bit 12:0 Source
    // Pitch (0x100010) e Window Width (0x10003C)
    // Devono essere uguali alla larghezza virtuale
    uint32 pitchVal = ((width & 0x1FFF) << 16) | (width & 0x1FFF);
    SM750_WREG32(SM750_2D_PITCH, pitchVal);
    SM750_WREG32(SM750_2D_WINDOW_WIDTH, pitchVal);
    
    // Wrap (0x10004C)
    // Lo impostiamo alle dimensioni massime per evitare che " wrappi" a metà schermo
    SM750_WREG32(SM750_2D_WRAP, (width << 16) | height);

    // 3. Imposta il Formato e Addressing (0x10001C)
    // Bit 21:20 = 10 (32-bpp)
    // Bit 19:16 = 0000 (XY Mode)
    // Bit 30 = 1 (XY mode per il pattern)
    uint32 format = (1U << 30) | (bpp_code << 20) | (0U << 16);
    SM750_WREG32(SM750_2D_STRETCH, format);

    // 4. Imposta le Basi (Source e Destination Base)
    // Questi registri (solitamente 0x100040 e 0x100044) dicono al chip 
    // l'offset zero della VRAM. Di solito si mettono a 0.
    // 31:28  Res      These bits are reserved.
    // 27     Ext      Memory Selection.
    //                 0: Local memory.
    //                 1: System memory.
    // 26     CS       Chip Select for System Memory.
    //                 0: CS0 of system memory.
    //                 1: CS1 of system memory.
    // 25:4   Address  Memory address of source window with 128-bit alignment.
    // 3:0    0000     These bits are hardwired to zeros.
    SM750_WREG32(SM750_2D_SOURCE_BASE, 0); 
    SM750_WREG32(SM750_2D_DEST_BASE, 0);
    
    // 4. Clipping (Fondamentale!): Se non impostato, il motore non scrive nulla
    // in 2D Clip TL Bit 13 = 1 attiva il clipping
    // 2D Clip TL (0x10002C)
    // Bit 31:16 = Top (0)
    // Bit 13    = Enable (1)
    // Bit 12    = Select (0: Write INSIDE enabled)
    // Bit 11:0  = Left (0)
    uint32 clipTL = (1 << 13);
    SM750_WREG32(SM750_2D_CLIP_TL, clipTL);

    // 2D Clip BR (0x100030)
    // Bit 31:16 = Bottom (height - 1) -> Maschera 0xFFFF
    // Bit 15:13 = Reserved (devono rimanere a 0)
    // Bit 12:0  = Right (width - 1)  -> Maschera 0x1FFF (13 bit) max 8191
    uint32 clipBR = (((height - 1) & 0xFFFF) << 16) | ((width - 1) & 0x1FFF);
    SM750_WREG32(SM750_2D_CLIP_BR, clipBR);
    //debug_printf("SM750_ACC: 2D engine succesfully initializated\n");
}

void sm750_fill_rectangle(engine_token *et, uint32 color, 
    fill_rect_params *params, uint32 count)
{
	CALLED();
	if (et == NULL) return;
	// 1. ACQUISIAMO IL LOCK (Essenziale per Haiku)
    gInfo->si->engine.lock.Lock();
    
    vuint32 *regs = gInfo->regs;

    for (uint32 i = 0; i < count; i++) {
        // 1. Calcoliamo i valori (X nei bit 28:16, Y nei bit 15:0)
        uint32 x = params[i].left & 0x1FFF;
        uint32 y = params[i].top & 0xFFFF;
        uint32 width = (params[i].right - params[i].left + 1) & 0x1FFF;
        uint32 height = (params[i].bottom - params[i].top + 1) & 0xFFFF;

        // Prepariamo i registri (Senza ancora scrivere il comando START)
        //SM750_WREG32(SM750_2D_FOREGROUND, color);
        SM750_WREG32(SM750_2D_FOREGROUND, 0xFFFF0000); //ROSSO
        
        // DESTINATION: X è 28:16, Y è 15:0
        SM750_WREG32(SM750_2D_DESTINATION, (x << 16) | y);
        
        // DIMENSION: Width è 28:16, Height è 15:0
        SM750_WREG32(SM750_2D_DIMENSION, (width << 16) | height);

        // 2. ORA aspettiamo che il chip sia libero dal compito precedente
        sm750_wait_engine_idle();

        // 3. SPARIAMO il comando (START + Rectangle Fill + ROP 0xCC)
        //uint32 cmd = (1U << 31) | (1U << 30) | (0x00001 << 16) | 0xCC;
        // ROP2?
        uint32 cmd = (1U << 31) | (1U << 30) | (1 << 15) | (0x00001 << 16) | 0x0C;
        SM750_WREG32(SM750_2D_CONTROL, cmd);
    }
    // 2. RILASCIAMO IL LOCK
    gInfo->si->engine.lock.Release();
}

void sm750_screen_to_screen_blit(engine_token *et, blit_params *p, uint32 count) {
	CALLED();
	if (et == NULL) return;
	// 1. ACQUISIAMO IL LOCK (Essenziale per Haiku)
    gInfo->si->engine.lock.Lock();
    
    vuint32 *regs = gInfo->regs;

    for (uint32 i = 0; i < count; i++) {
        uint32 cmd = (1U << 31) | (0x00000 << 16) | 0xCC;
        uint32 src_x = p[i].src_left;
        uint32 src_y = p[i].src_top;
        uint32 dst_x = p[i].dest_left;
        uint32 dst_y = p[i].dest_top;

        // Gestione direzione (Bottom-to-Top se necessario)
        if (dst_y > src_y || (dst_y == src_y && dst_x > src_x)) {
            cmd |= (1 << 27); // Right-to-Left / Bottom-to-Top
            src_x += p[i].width - 1;
            src_y += p[i].height - 1;
            dst_x += p[i].width - 1;
            dst_y += p[i].height - 1;
        }

        // Scrittura registri con X in 28:16 e Y in 15:0
        SM750_WREG32(SM750_2D_SOURCE, ((src_x & 0x1FFF) << 16) | (src_y & 0xFFFF));
        SM750_WREG32(SM750_2D_DESTINATION, ((dst_x & 0x1FFF) << 16) | (dst_y & 0xFFFF));
        SM750_WREG32(SM750_2D_DIMENSION, ((p[i].width & 0x1FFF) << 16) | (p[i].height & 0xFFFF));

        sm750_wait_engine_idle();
        SM750_WREG32(SM750_2D_CONTROL, cmd);
    }
    // 2. RILASCIAMO IL LOCK
    gInfo->si->engine.lock.Release();
}
void sm750_invert_rectangle(engine_token *et, fill_rect_params *list, uint32 count) {
	CALLED();
    if (et == NULL) return;
    
    // Protezione col Benaphore
    gInfo->si->engine.lock.Lock();
    
    vuint32 *regs = gInfo->regs;

    for (uint32 i = 0; i < count; i++) {
        
        uint32 x = list[i].left & 0x1FFF;
        uint32 y = list[i].top & 0xFFFF;
        uint32 width = (list[i].right - list[i].left + 1) & 0x1FFF;
        uint32 height = (list[i].bottom - list[i].top + 1) & 0xFFFF;

        // Coordinate e Dimensioni (Registro 0x100000 e 0x100008)
        SM750_WREG32(SM750_2D_DESTINATION, (x << 16) | y);
        SM750_WREG32(SM750_2D_DIMENSION, (width << 16) | height);
        
        // Aspetta che l'engine sia libero
        sm750_wait_engine_idle();
        
        // Control (0x10000C):
        // Bit 31: Start (1)
        // Bit 20:16: Command BitBlt (00000)
        // Bit 15: ROP2 Select (1)
        // Bit 7:0: ROP2 NOT DEST (0x55)
        uint32 cmd = (1U << 31) | (1U << 15) | 0x55;
        
        SM750_WREG32(SM750_2D_CONTROL, cmd);
    }

    gInfo->si->engine.lock.Release();
}
void sm750_fill_span(engine_token *et, uint32 color, uint16 *spans, uint32 count) {
	CALLED();
    if (et == NULL) return;

    gInfo->si->engine.lock.Lock();
    vuint32 *regs = gInfo->regs;
    
    SM750_WREG32(SM750_2D_FOREGROUND, color);

    for (uint32 i = 0; i < count; i++) {
        uint16 y = spans[i * 3];
        uint16 x = spans[i * 3 + 1];
        uint16 width = spans[i * 3 + 2];

        // Dest (0x100004): X(28:16), Y(15:0)
        SM750_WREG32(SM750_2D_DESTINATION, (uint32(x & 0x1FFF) << 16) | (y & 0xFFFF));
        // Dim (0x100008): W(28:16), H(15:0) = 1
        SM750_WREG32(SM750_2D_DIMENSION, (uint32(width & 0x1FFF) << 16) | 1);

        sm750_wait_engine_idle();
        // Bit 31: Start
        // Bit 30: Color Pattern (1)
        // Bit 20:16: Cmd 00001 (Rect Fill)
        // Bit 15: ROP2 Select (1)
        // Bit 7:0: ROP2 Pattern Copy (0x0C)
        uint32 cmd = (1U << 31) | (1U << 30) | (1U << 16) | (1U << 15) | 0x0C;
        SM750_WREG32(SM750_2D_CONTROL, cmd);
    }

    gInfo->si->engine.lock.Release();
}
// Questo thread "vive" nell'accelerante e gestisce i cambi di buffer
//uint32 source = si->card_info->is_panel ? SM750_DISP_PANEL_VIDEO_FB0_ADDR : SM750_DISP_CRT_FB_ADDR;
int32 sm750_vblank_service_thread(void *arg)
{
    accelerant_info *ai = (accelerant_info *)arg;
    shared_info *si = ai->si;
    vuint32* regs = ai->regs; // Per le macro
    
    debug_printf("SM750_ACC: Thread vblank in ascolto su SEM ID: %d\n", si->vblank_sem);

    while (atomic_get(&si->irq_enabled) > 0) {
    	//status_t err = acquire_sem(si->vblank_sem);
    	status_t err = acquire_sem_etc(si->vblank_sem, 1, B_CAN_INTERRUPT, 0);
        // Aspettiamo l'interrupt dal kernel
        if (err == B_OK) {
            
            // C'è un nuovo buffer che aspetta il V-Sync?
            if (ai->overlay_active && ai->next_buffer_to_show != NULL) {
                
                // Prendiamo l'indirizzo fisico del buffer
                uint32 offset = (uint32)(addr_t)ai->next_buffer_to_show->buffer_dma;
                
                // Scriviamo nel registro del Panel Video Source (0x080044)
                SM750_WREG32(SM750_DISP_PANEL_VIDEO_FB0_ADDR, offset);
                
                // Aggiorniamo lo stato locale
                ai->current_ob = ai->next_buffer_to_show;
                ai->next_buffer_to_show = NULL;
            }
            // 3. ORA segnaliamo al resto del mondo (App, Media Player) che il V-Sync è avvenuto
            // Solo se qualcuno lo ha creato!
            if (si->vblank_sync_sem > 0)   
                // Svegliamo l'eventuale chiamata bloccante in attesa del flip
                release_sem(si->vblank_sync_sem);
        } else {
            // Errore grave (permessi, semaforo distrutto, ecc.)
            // Snooze per evitare di saturare la CPU in caso di errore
            debug_printf("SM750_ACC: Errore acquire_sem: %s\n", strerror(err));
            snooze(50000); // 50ms di pausa per far respirare il sistema
            if (err == B_BAD_SEM_ID) break; // Esci se il semaforo è morto
        }
    }
    return B_OK;
}
