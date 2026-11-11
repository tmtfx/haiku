/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef FINGERPRINT_DEVICE_H
#define FINGERPRINT_DEVICE_H


#include <SupportDefs.h>


// Abstract interface for a fingerprint sensor driven by fingerprint_server.
//
// Scope is deliberately limited to the "area sensor, raw grayscale image,
// matching done host-side" device family (the same family libfprint's "img"
// framework covers: DigitalPersona/URU4xxx, AuthenTec AES, UPEK TouchStrip,
// ...). Swipe sensors (need host-side stitching of partial frames) and
// match-on-chip sensors (enroll/identify happens on the device, no raw
// image ever reaches the host) have a fundamentally different interaction
// shape and should get their own interface if/when one is actually
// implemented and tested - don't force-fit them into this one speculatively.
//
// All calls are expected to run from a single worker thread at a time per
// device instance; implementations are not required to be thread-safe.
// (See Uru4000Device for why: Haiku's usb_raw serializes transfers per
// device even across handles, so a per-device background polling thread
// alongside on-demand transfers from another thread will deadlock.)
class FingerprintDevice {
public:
	virtual							~FingerprintDevice() {}

	virtual	status_t				Open(const char* path) = 0;
	virtual	void					Close() = 0;
	virtual	bool					IsOpen() const = 0;

	// Full power-on handshake. Must be called with no other transfer to
	// this device in flight.
	virtual	status_t				Activate() = 0;

	// Blocks until a finger is detected (or an error/timeout occurs).
	virtual	status_t				WaitForFingerOn(bool* gotFinger) = 0;

	// Captures one raw image into buffer (at least CaptureBufferSize()
	// bytes). Returns the actual byte count transferred (may legitimately
	// be less than CaptureBufferSize() - a short capture is often normal,
	// not an error) or a negative status_t.
	virtual	ssize_t					CaptureImage(void* buffer,
										size_t size) = 0;

	// Blocks until the finger is lifted (or an error/timeout occurs).
	virtual	status_t				WaitForFingerOff(bool* gotRelease) = 0;

	// Powers the sensor down. Safe to call multiple times / when not
	// armed.
	virtual	void					Deactivate() = 0;

	// Image geometry, specific to this device/chipset - callers must not
	// assume a fixed size across different FingerprintDevice
	// implementations.
	virtual	int32					ImageWidth() const = 0;
	virtual	int32					ImageHeight() const = 0;

	// Number of bytes at the start of a CaptureImage() buffer that are
	// device-specific header/metadata, before the raw pixel data begins.
	virtual	size_t					ImageHeaderSize() const = 0;

	// Minimum buffer size CaptureImage() needs (header + full image).
	virtual	size_t					CaptureBufferSize() const = 0;
};


#endif // FINGERPRINT_DEVICE_H
