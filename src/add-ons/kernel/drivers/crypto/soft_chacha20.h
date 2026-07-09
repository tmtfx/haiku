/*
 * Software ChaCha20 for Haiku kernel crypto
 * Copyright 2026, Fabio Tomat
 * MIT License
 */
#ifndef _SOFT_CHACHA20_H_
#define _SOFT_CHACHA20_H_


#include <SupportDefs.h>

struct ChaCha20Context {
    uint32 state[16];
};

void chacha20_init(ChaCha20Context* ctx, const uint8* key, const uint8* nonce, uint32 counter);
void chacha20_process(ChaCha20Context* ctx, const uint8* in, uint8* out, size_t len);

#endif // _SOFT_CHACHA20_H_
