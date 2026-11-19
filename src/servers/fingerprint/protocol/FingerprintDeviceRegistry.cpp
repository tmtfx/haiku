/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include "FingerprintDeviceRegistry.h"

#include <new>

#include "FingerprintDevice.h"
#include "uru4000/Uru4000Device.h"
#include "uru4000/Uru4000Protocol.h"


namespace {

struct DeviceEntry {
	uint16 vendorID;
	uint16 productID;
	FingerprintDevice* (*create)();
};

FingerprintDevice*
CreateUru4000Device()
{
	return new(std::nothrow) Uru4000Device();
}

const DeviceEntry kKnownDevices[] = {
	{ Uru4000::kVendorID, Uru4000::kProductID, &CreateUru4000Device },
	// Add new supported readers here - {vendorID, productID, factory} -
	// nothing else in fingerprint_server needs to change.
};

const size_t kKnownDeviceCount = sizeof(kKnownDevices)
	/ sizeof(kKnownDevices[0]);

} // namespace


bool
FingerprintDeviceRegistry::Matches(uint16 vendorID, uint16 productID)
{
	for (size_t i = 0; i < kKnownDeviceCount; i++) {
		if (kKnownDevices[i].vendorID == vendorID
			&& kKnownDevices[i].productID == productID) {
			return true;
		}
	}
	return false;
}


FingerprintDevice*
FingerprintDeviceRegistry::Create(uint16 vendorID, uint16 productID)
{
	for (size_t i = 0; i < kKnownDeviceCount; i++) {
		if (kKnownDevices[i].vendorID == vendorID
			&& kKnownDevices[i].productID == productID) {
			return kKnownDevices[i].create();
		}
	}
	return NULL;
}
