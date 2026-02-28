/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef HYBRID_SHA_OPT_H
#define HYBRID_SHA_OPT_H

#include "soft_sha.h"
#include <SupportDefs.h>

//void hybrid_set_use_avx2(bool enable);
// Funzioni di interfaccia per il dispatcher

//#ifdef __cplusplus
//extern "C" {
//#endif

void hybrid_SHA1_init(SoftSHA1Context* ctx, size_t outLen);
void hybrid_SHA1_update(SoftSHA1Context* ctx, const uint8* in, size_t inLen);
void hybrid_SHA1_finalize(SoftSHA1Context* ctx, uint8* out);

void hybrid_SHA224_init(SoftSHA256Context* ctx, size_t outLen);
void hybrid_SHA256_init(SoftSHA256Context* ctx, size_t outLen);
void hybrid_SHA256_update(SoftSHA256Context* ctx, const uint8* in, size_t inLen);
void hybrid_SHA256_finalize(SoftSHA256Context* ctx, uint8* out);
void hybrid_SHA224_finalize(SoftSHA256Context* ctx, uint8* out);

void hybrid_SHA384_init(SoftSHA512Context* ctx, size_t outLen);
void hybrid_SHA512_init(SoftSHA512Context* ctx, size_t outLen);
void hybrid_SHA512_update(SoftSHA512Context* ctx, const uint8* in, size_t inLen);
void hybrid_SHA512_finalize(SoftSHA512Context* ctx, uint8* out);
void hybrid_SHA384_finalize(SoftSHA512Context* ctx, uint8* out);
//#ifdef __cplusplus
//}
//#endif
#endif
