/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef HYBRID_BLAKE_OPT_H
#define HYBRID_BLAKE_OPT_H

#include "soft_blake.h"
#include <SupportDefs.h>

void hybrid_set_use_avx2(bool enable);
// Funzioni di interfaccia per il dispatcher
void hybrid_blake2s_init(SoftBlake2sContext* ctx, size_t outLen);
void hybrid_blake2s_update(SoftBlake2sContext* ctx, const uint8* in, size_t inLen);
void hybrid_blake2s_finalize(SoftBlake2sContext* ctx, uint8* out);

void hybrid_blake2b_init(SoftBlake2bContext* ctx, size_t outLen);
void hybrid_blake2b_update(SoftBlake2bContext* ctx, const uint8* in, size_t inLen);
void hybrid_blake2b_finalize(SoftBlake2bContext* ctx, uint8* out);


#endif
