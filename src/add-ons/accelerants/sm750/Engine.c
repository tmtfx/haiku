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

#define CALLED() debug_printf("SM750_ACC: %s\n", __FUNCTION__)

extern accelerant_info *gInfo;

static status_t sm750_calc_pll(uint32 target_khz, uint32* out_M, uint32* out_N, uint32* out_OD)
{
    uint32 M, N, OD_idx;
    uint32 best_M = 0, best_N = 0, best_OD_idx = 0;
    float f_ref = 24.0f; // MHz
    float target = (float)target_khz / 1000.0f; // Convertiamo in MHz
    float min_diff = 1000.0f;

    // OD per SM750 può dividere per 1, 2, 4, 8, 16... 
    // Definiamo i divisori reali supportati
    uint32 od_divisors[] = {1, 2, 4, 8, 16};
    
    for (OD_idx = 0; OD_idx < 5; OD_idx++) {
        uint32 div = od_divisors[OD_idx];
        
        for (N = 2; N <= 15; N++) {
            // Formula: M = (Target * Div * N) / F_ref
            M = (uint32)((target * (float)div * (float)N) / f_ref + 0.5f);
            
            if (M < 2 || M > 255) continue;

            // Il VCO deve essere tra MIN e MAX
            // VCO = F_ref * (M / N)
            float vco = f_ref * ((float)M / (float)N);
            if (vco < 240.0f || vco > 480.0f) continue;

            float actual = vco / (float)div;
            float diff = (actual > target) ? (actual - target) : (target - actual);

            if (diff < min_diff) {
                min_diff = diff;
                best_M = M;
                best_N = N;
                best_OD_idx = OD_idx; // Indice del divisore
            }
        }
    }

    if (best_M == 0) return B_ERROR;

    *out_M = best_M;
    *out_N = best_N;
    // out_OD restituirà l'indice da scrivere nei bit [3:0]
    *out_OD = best_OD_idx; 
    
    return B_OK;
}
/* versione sbagliata!!!
void
sm750_program_pll(uint32 target_khz, bool is_panel)
{
    uint32 m, n, od_idx;
    vuint32 *regs = gInfo->regs;
    
    if (sm750_calc_pll(target_khz, &m, &n, &od_idx) != B_OK) {
        debug_printf("SM750: Errore calcolo PLL per %u kHz\n", target_khz);
        return;
    }

    uint32 pll_reg = 0;

    // 1. Power On (Bit 17)
    pll_reg |= (1 << 17);

    // 2. Mappatura Divisori (POD e OD)
    // od_idx: 0=div1, 1=div2, 2=div4, 3=div8, 4=div16
    if (od_idx <= 3) {
        // Usiamo solo OD (bit 13:12), POD rimane 0 (div 1)
        pll_reg |= (od_idx << 12);
    } else {
        // Per div 16, usiamo POD=01 (div 2) e OD=11 (div 8) -> 2*8=16
        pll_reg |= (1 << 14); // POD = 2
        pll_reg |= (3 << 12); // OD = 8
    }

    // 3. Valori N e M
    // Nota: Spesso nel registro va scritto il valore reale, 
    // ma se vedi che la frequenza è sballata, proveremo con (n-1)
    pll_reg |= ((n & 0x0F) << 8);
    pll_reg |= (m & 0xFF);

    // 4. Scelta del registro (0x5C per Primary/Panel, 0x60 per Secondary/CRT)
    uint32 reg_offset = is_panel ? SM750_DISP_PANEL_PLL : SM750_DISP_CRT_PLL;
    
    // Scrittura
    SM750_WREG32(reg_offset, pll_reg);
    
    // Wait for lock (importante!)
    snooze(1500); 
    
    debug_printf("SM750: PLL %s programmato a %" B_PRIu32 " kHz (Reg 0x%" B_PRIx32 ": 0x%08" B_PRIx32 ")\n", 
                 is_panel ? "PRI" : "SEC", target_khz, reg_offset, pll_reg);
}*/
void
sm750_program_pll(uint32 target_khz, bool is_panel)
{
	vuint32 *regs = gInfo->regs;
    uint32 m, n, od_idx;
    uint32 pod_val = 0;
    uint32 od_val = 0;
    
    // 1. Calcolo i parametri M, N e il divisore totale
    if (sm750_calc_pll(target_khz, &m, &n, &od_idx) != B_OK) {
        debug_printf("SM750: Errore calcolo PLL per %u kHz\n", target_khz);
        return;
    }

    // 2. Mappatura od_idx (divisore totale) ai bit POD (15:14) e OD (13:12)
    // Valido per entrambi i registri 0x5C e 0x60
    switch (od_idx) {
        case 1:  pod_val = 0; od_val = 0; break; // 1 * 1
        case 2:  pod_val = 0; od_val = 1; break; // 1 * 2
        case 4:  pod_val = 0; od_val = 2; break; // 1 * 4
        case 8:  pod_val = 0; od_val = 3; break; // 1 * 8
        case 16: pod_val = 1; od_val = 3; break; // 2 * 8
        case 32: pod_val = 2; od_val = 3; break; // 4 * 8
        case 64: pod_val = 3; od_val = 3; break; // 8 * 8
        default:
            debug_printf("SM750: od_idx %u non supportato, uso default div 1\n", od_idx);
            pod_val = 0; od_val = 0;
            break;
    }

    // 3. Costruzione del registro unico
    uint32 pll_reg = 0;
    pll_reg |= (1 << 17);               // PD: Power On
    pll_reg |= (pod_val << 14);         // POD
    pll_reg |= (od_val << 12);          // OD
    pll_reg |= ((n & 0x0F) << 8);       // N
    pll_reg |= (m & 0xFF);              // M

    // 4. Selezione del registro di destinazione
    uint32 reg_offset = is_panel ? SM750_DISP_PANEL_PLL : SM750_DISP_CRT_PLL;
    
    // Scrittura hardware
    SM750_WREG32(reg_offset, pll_reg);
    
    // 5. Attesa stabilizzazione (Lock)
    snooze(1500); 
    
    debug_printf("SM750: PLL %s programmato a %u kHz\n", 
                 is_panel ? "PANEL (0x5C)" : "CRT (0x60)", target_khz);
    debug_printf("SM750: Reg 0x%x = 0x%08x (M:%u N:%u Div:%u)\n", 
                 reg_offset, pll_reg, m, n, od_idx);
}
/* vecchia usa l'helper che allinea a 16byte
status_t
sm750_move_display_area(uint16 h_display_start, uint16 v_display_start)
{
    shared_info *si = gInfo->si;
    vuint32 *regs = gInfo->regs;
    
    // Calcoliamo l'offset in byte basandoci sulla profondità di colore attuale
    // Nota: dobbiamo sapere quanti byte per pixel stiamo usando.
    // Per ora facciamo un calcolo generico, poi lo affineremo.
    uint32 bytes_per_pixel = (si->dm.space == B_RGB32) ? 4 : 2;
    uint32 start_addr = (v_display_start * si->dm.virtual_width + h_display_start) * bytes_per_pixel;

    // Registro: CRT Display Start Address (Pagina 52 del datasheet)
    // Offset 0x8001C per il CRT
    SM750_WREG32(SM750_DISP_CRT_FB_ADDR, start_addr & 0x07FFFFFF); 

    return B_OK;
}*/
status_t
sm750_move_display_area(uint16 h_display_start, uint16 v_display_start)
{
    shared_info *si = gInfo->si;
    
    uint32 bytes_per_pixel = (si->dm.space == B_RGB32) ? 4 : 2;
    
    // L'offset è (Y * LarghezzaVirtuale + X) * Bpp
    uint32 start_addr = (v_display_start * si->dm.virtual_width + h_display_start) * bytes_per_pixel;

    // Usiamo l'helper che gestisce allineamento e bit riservati
    return sm750_set_crt_fb_addr(start_addr);
}



/* Info sul clone (Haiku le usa per condividere l'accelerante tra app) */
uint32 sm750_accelerant_clone_info_size(void) {
	CALLED();
    // clone info is device name, so return its maximum size
	return B_PATH_NAME_LENGTH;
}

void sm750_get_accelerant_clone_info(void *data) {
	CALLED();
    strcpy((char *)data, gInfo->si->device_path);
}

status_t sm750_clone_accelerant(void* info)
{
    CALLED();
    char path[B_PATH_NAME_LENGTH];
    
    // Costruiamo il path completo
    strcpy(path, "/dev/");
    strlcat(path, (const char*)info, sizeof(path));

    int fd = open(path, O_RDWR);
    if (fd < 0) return errno;

    // Inizializziamo l'accelerante clone
    status_t status = sm750_init_accelerant(fd);
    if (status != B_OK) {
        close(fd);
        return status;
    }

    return B_OK;
}

status_t sm750_get_accelerant_device_info(accelerant_device_info *adi) {
	CALLED();
    adi->version = 1;
    strcpy(adi->name, "Silicon Motion SM750");
    strcpy(adi->chipset, "SM750");
    strcpy(adi->serial_no, "v0.1");
    adi->memory = gInfo->si->card_info.mem_size;
    adi->dac_speed = 300000; // 300MHz
    return B_OK;
}

/* Limiti del Pixel Clock (Richiesto!) */
status_t sm750_get_pixel_clock_limits(display_mode *dm, uint32 *low, uint32 *high) {
	CALLED();
    *low = 10000;   // 10 MHz
    *high = 300000; // 300 MHz
    return B_OK;
}

/* Engine Management */
uint32 sm750_accelerant_engine_count(void) {
    return 0; // Per ora diciamo 0, così Haiku disegna tutto via software (più lento ma sicuro)
}

status_t sm750_acquire_engine(uint32 capabilities, uint32 max_priority, 
                               sync_token *st, engine_token **et) {
    return B_ERROR; // Non abbiamo motori da dare
}

void sm750_release_engine(engine_token *et, sync_token *st) {
    /* Nulla da fare */
}

void sm750_wait_engine_idle(void) {
    /* Se non usiamo l'engine, è sempre idle! */
}

status_t
sm750_get_sync_token(engine_token *et, sync_token *st)
{
    // Non avendo un engine reale attivo, non c'è nulla da sincronizzare
    return B_OK;
}

status_t
sm750_sync_to_token(sync_token *st)
{
    // Siamo sempre in sync se non c'è accelerazione hardware!
    return B_OK;
}
