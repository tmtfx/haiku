// Small compatibility wrapper implementing AES_Setkey/AES_Encrypt/AES_Decrypt
// using OpenSSL AES. Stores a pointer to OpenSSL AES_KEY inside the
// provided AES_CTX memory (opaque usage).

#include <openssl/aes.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "aes.h"

int AES_Setkey(AES_CTX *ctx, const uint8_t *key, int len)
{
	AES_KEY *k = malloc(sizeof(AES_KEY));
	if (!k)
		return -1;
	#pragma GCC diagnostic push
	#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
	if (AES_set_encrypt_key(key, len * 8, k) < 0) {
		free(k);
		#pragma GCC diagnostic pop
		return -1;
	}
	#pragma GCC diagnostic pop
	// store pointer into ctx->sk (use memcpy to avoid strict-aliasing)
	memset(ctx->sk, 0, sizeof(ctx->sk));
	memcpy(ctx->sk, &k, sizeof(void*));
	ctx->num_rounds = 0;
	return 0;
}

void AES_Encrypt(AES_CTX *ctx, const uint8_t *src, uint8_t *dst)
{
	AES_KEY *k = NULL;
	memcpy(&k, ctx->sk, sizeof(void*));
	if (k == NULL) {
		// fallback: zero output
		memset(dst, 0, 16);
		return;
	}
	#pragma GCC diagnostic push
	#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
	AES_encrypt(src, dst, k);
	#pragma GCC diagnostic pop
}

void AES_Decrypt(AES_CTX *ctx, const uint8_t *src, uint8_t *dst)
{
	AES_KEY *k = NULL;
	memcpy(&k, ctx->sk, sizeof(void*));
	if (k == NULL) {
		memset(dst, 0, 16);
		return;
	}
	#pragma GCC diagnostic push
	#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
	AES_decrypt(src, dst, k);
	#pragma GCC diagnostic pop
}
