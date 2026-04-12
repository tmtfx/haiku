/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include <Accelerant.h>
#include <Debug.h>
#include "DriverInterface.h"
#include "protos.h"
#include "sm750_macros.h"

extern accelerant_info *gInfo;

/* Helper per scrivere nei registri MMIO usando la struttura shared_info */
/* Usiamo gInfo->shared_info->regs che abbiamo visto nel DriverInterface.h */
#define WRITE_REG(offset, val) (*(vuint32 *)((uint8 *)si->regs + (offset)) = (val))
#define READ_REG(offset) (*(vuint32 *)((uint8 *)si->regs + (offset)))

void program_crt_pll(uint32 target_khz, vuint32 *regs) {
    uint32 m, n, od, best_m = 2, best_n = 1, best_od = 0;
    uint32 f_ref = 24000;
    uint32 min_err = 0xFFFFFFFF;

    for (od = 0; od <= 2; od++) {
        for (n = 1; n <= 63; n++) {
            for (m = 2; m <= 255; m++) {
                uint32 cur = (f_ref * m / n) >> od;
                uint32 err = abs((int32)target_khz - (int32)cur);
                if (err < min_err) {
                    min_err = err; best_m = m; best_n = n; best_od = od;
                }
            }
        }
    }
    // Scrittura registro SM750_SYS_PLL_CTRL
    SYS_W(PLL_CTRL, (1 << 18) | (best_m << 8) | (best_od << 6) | best_n);
}
