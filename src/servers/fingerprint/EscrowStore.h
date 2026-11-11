/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef ESCROW_STORE_H
#define ESCROW_STORE_H


#include <String.h>
#include <SupportDefs.h>


// Stores the single active sequence-escrow record ("K", 32 bytes, plus the
// ordered 4-fingerId sequence that must be matched live before K is
// released), under the user's private settings directory (owner-only
// permissions). fingerprint_server never sees the master password or the
// USB stick that holds its encrypted form - see Fase 1e (revisionata) in
// the project plan for the full keystore wrap-key design.
class EscrowStore {
public:
	static const int32			kSequenceLength = 4;

								EscrowStore();
								~EscrowStore();

			status_t			Init();

			status_t			Set(const int32* sequence, const uint8* key,
									size_t keyLength);
			status_t			Get(int32* outSequence, uint8* outKey,
									size_t keyLength);
			status_t			Clear();
			bool				HasEscrow();

private:
			BString				fPath;
};


#endif // ESCROW_STORE_H
