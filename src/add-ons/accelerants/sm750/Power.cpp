/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include <Accelerant.h>
#include <Debug.h>
#include "DriverInterface.h"
#include "protos.h"
#include "sm750_macros.h"

extern "C" uint32 sm750_dpms_mode(void)
{
    // Per ora restituiamo sempre ON per non bloccare il sistema
    return B_DPMS_ON;
}

// Restituisce cosa sa fare il chip
uint32 sm750_dpms_capabilities(void)
{
    // Specifichiamo i modi standard
    return B_DPMS_ON | B_DPMS_STAND_BY | B_DPMS_SUSPEND | B_DPMS_OFF;
}

status_t sm750_set_dpms_mode(uint32 mode) {
    // Per ora non facciamo nulla, ma restituiamo successo
    return B_OK;
}
