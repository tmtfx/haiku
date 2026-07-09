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
    // For now let's return always ON for not locking the system
    return B_DPMS_ON;
}

// Restituisce cosa sa fare il chip
uint32 sm750_dpms_capabilities(void)
{
    // Specify standard modes
    return B_DPMS_ON | B_DPMS_STAND_BY | B_DPMS_SUSPEND | B_DPMS_OFF;
}

status_t sm750_set_dpms_mode(uint32 mode) {
    // For now don't do nothing, return success
    return B_OK;
}
