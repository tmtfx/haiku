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
    debug_printf("SM750_PLL: target kH %u, m %u, n %u\n",target_khz,m,n);

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
    debug_printf("SM750_PLL: pod_bits %u, od_bits %u, n %u, m %u\n",pod_bits,od_bits,n,m);
    pll_reg |= (1 << 17);               // PD: PLL Power On
    pll_reg |= (pod_bits << 14);        // POD
    pll_reg |= (od_bits << 12);         // OD
    pll_reg |= ((n & 0x0F) << 8);       // N (bit 11:8)
    pll_reg |= (m & 0xFF);              // M (bit 7:0)
    
    // impostiamo entrambi i pll alla stessa frequenza giusto per vedere se cambia qualcosa
    SM750_WREG32(SM750_DISP_PANEL_PLL,pll_reg);
    SM750_WREG32(SM750_DISP_CRT_PLL,pll_reg);
    uint32 ctrl_offset = is_panel ? SM750_CRT_CONTROL : SM750_PANEL_CONTROL;
    SM750_WREG32(ctrl_offset,0x0); // disattiva tutto in ctr o in panel
    snooze(1500); 
    debug_printf("SM750_PLL: Programmazione %s a %u kHz\n", is_panel ? "PANEL" : "CRT", target_khz);
    debug_printf("SM750_PLL: M:%u N:%u Div:%u -> Reg 0x%08x\n", m, n, div_val, pll_reg);
}

status_t
sm750_move_display_area(uint16 h_display_start, uint16 v_display_start)
{
    shared_info *si = gInfo->si;
    
    uint32 bytes_per_pixel = (si->dm.space == B_RGB32) ? 4 : 2;
    
    // L'offset è (Y * LarghezzaVirtuale + X) * Bpp
    uint32 start_addr = (v_display_start * si->dm.virtual_width + h_display_start) * bytes_per_pixel;

    // Usiamo l'helper che gestisce allineamento e bit riservati
    return sm750_set_fb_addr(start_addr, si->card_info.is_panel);
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
