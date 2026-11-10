/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef ESCROW_STORE_H
#define ESCROW_STORE_H


#include <String.h>
#include <SupportDefs.h>


// Stores the 32-byte escrow keys ("K") that keystore_server hands us via
// B_FP_SET_ESCROW, one per enrolled finger, under the user's private
// settings directory (owner-only permissions). fingerprint_server never
// sees the master password itself, only this opaque key - see Fase 1e in
// the project plan for the full keystore wrap-key design.
class EscrowStore {
public:
								EscrowStore();
								~EscrowStore();

			status_t			Init();

			status_t			Set(int32 fingerId, const uint8* key,
									size_t keyLength);
			status_t			Get(int32 fingerId, uint8* outKey,
									size_t keyLength);
			status_t			Delete(int32 fingerId);

private:
			BString				fEscrowDir;
			status_t			_EntryPath(int32 fingerId, BString& path);
};


#endif // ESCROW_STORE_H
