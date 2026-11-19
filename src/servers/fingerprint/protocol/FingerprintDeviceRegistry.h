/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef FINGERPRINT_DEVICE_REGISTRY_H
#define FINGERPRINT_DEVICE_REGISTRY_H


#include <SupportDefs.h>

class FingerprintDevice;


// Maps a USB vendor/product ID pair to a FingerprintDevice implementation.
// Adding support for a new reader model means adding one entry to the
// table in the .cpp - nothing else in fingerprint_server needs to change.
class FingerprintDeviceRegistry {
public:
	static	bool					Matches(uint16 vendorID,
										uint16 productID);

	// Returns a new instance for the given VID/PID, or NULL if no known
	// driver matches. Caller owns the returned object.
	static	FingerprintDevice*		Create(uint16 vendorID,
										uint16 productID);
};


#endif // FINGERPRINT_DEVICE_REGISTRY_H
