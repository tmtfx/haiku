/*
 * Software Pseudo-Random Number Generator (CSPRNG)
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include "SoftCPURNG.h"
#include "BCryptoCore.h"
#include <crypto/BCryptoDefs.h>
#include "BCryptoAlgorithm.h"
#include <lock.h>
#include <string.h>
#include <KernelExport.h>

static mutex sSoftRNGMutex;
static uint64 sGeneratorState[4];
static bool sGeneratorInitialized = false;

static inline uint64
rotl(const uint64 x, int k)
{
	return (x << k) | (x >> (64 - k));
}

static uint64
xoshiro256_next(uint64 s[4])
{
	const uint64 result = rotl(s[1] * 5, 7) * 9;

	const uint64 t = s[1] << 17;

	s[2] ^= s[0];
	s[3] ^= s[1];
	s[1] ^= s[2];
	s[0] ^= s[3];

	s[2] ^= t;

	s[3] = rotl(s[3], 45);

	return result;
}

static uint64
splitmix64(uint64& x)
{
	x += 0x9E3779B97F4A7C15ULL;
	uint64 z = x;
	z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
	z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
	return z ^ (z >> 31);
}

static void
soft_rng_seed(uint64 s[4])
{
	uint64 seed = (uint64)system_time();
#if defined(__x86_64__) || defined(__i386__)
	uint32 lo, hi;
	__asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
	seed ^= (((uint64)hi << 32) | lo);
#endif
	seed ^= (uintptr_t)s;
	uintptr_t stackVar;
	seed ^= (uintptr_t)&stackVar;

	if (seed == 0)
		seed = 0x123456789ABCDEF0ULL;

	uint64 x = seed;
	s[0] = splitmix64(x);
	s[1] = splitmix64(x);
	s[2] = splitmix64(x);
	s[3] = splitmix64(x);

	if ((s[0] | s[1] | s[2] | s[3]) == 0) {
		s[0] = 0x123456789ABCDEF0ULL;
		s[1] = 0x0FEDCBA987654321ULL;
		s[2] = 0x5A5A5A5A5A5A5A5AULL;
		s[3] = 0xA5A5A5A5A5A5A5A5ULL;
	}
}

static status_t
soft_rng_generate(uint8* buffer, size_t length)
{
	mutex_lock(&sSoftRNGMutex);

	if (!sGeneratorInitialized) {
		soft_rng_seed(sGeneratorState);
		sGeneratorInitialized = true;
	}

	size_t generated = 0;
	while (generated < length) {
		uint64 val = xoshiro256_next(sGeneratorState);
		size_t step = sizeof(uint64);
		size_t toCopy = (length - generated < step) ? (length - generated) : step;
		memcpy(buffer + generated, &val, toCopy);
		generated += toCopy;
	}

	mutex_unlock(&sSoftRNGMutex);
	return B_OK;
}

static status_t
soft_rng_process(BCryptoRequest* request)
{
	for (size_t i = 0; i < request->vectorCount; i++) {
		status_t st = soft_rng_generate(
			(uint8*)request->destination[i].iov_base,
			request->destination[i].iov_len
		);
		if (st != B_OK)
			return st;
	}
	return B_OK;
}

status_t
BInitSoftCPURNG()
{
	mutex_init(&sSoftRNGMutex, "soft cpu rng lock");
	sGeneratorInitialized = false;

	static BCryptoAlgorithm sSoftCPURNG = {
		.algorithm = B_CRYPTO_RNG,
		.mode = B_CRYPTO_MODE_ANY,
		.flags = B_CRYPTO_ALG_SOFTWARE,
		.name      = "RNG (Software)",
		.priority = 10,
		.Process = soft_rng_process
	};

	return BRegisterCryptoAlgorithm(&sSoftCPURNG);
}
