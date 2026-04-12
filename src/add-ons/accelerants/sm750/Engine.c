/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include <Accelerant.h>
#include <Debug.h>
#include <SupportDefs.h>
#include "DriverInterface.h"
#include "sm750_macros.h"
#include "protos.h"

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
}

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
}
