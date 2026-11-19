/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef URU4000_DEVICE_H
#define URU4000_DEVICE_H


#include <USBKit.h>

#include "../FingerprintDevice.h"
#include "Uru4000Protocol.h"


// Thin wrapper around a BUSBDevice implementing the URU4000 register
// protocol (HWSTAT handshake, finger-presence interrupts, raw image
// capture). IMPORTANT: Haiku's usb_raw serializes ALL transfers to a
// given USB device, even across separate open handles - a blocking
// transfer on one thread stalls any other transfer to the same device
// issued from elsewhere. This class is therefore NOT thread-safe and
// must only ever be driven sequentially from a single worker thread at
// a time (confirmed empirically during the Phase 0 spike).
class Uru4000Device : public FingerprintDevice {
public:
								Uru4000Device();
	virtual						~Uru4000Device();

	virtual	status_t			Open(const char* path);
	virtual	void				Close();
	virtual	bool				IsOpen() const { return fDevice != NULL; }

			uint16				VendorID() const;
			uint16				ProductID() const;

			// Full power-on handshake: reads/writes REG_HWSTAT until the
			// device reports powered-up, then reads one interrupt packet
			// (expected to be IRQDATA_SCANPWR_ON). Must be called with no
			// other transfer in flight (true on a freshly opened device).
	virtual	status_t			Activate();

			// Arms the sensor to wait for a finger and blocks on exactly
			// one interrupt read. Returns B_OK with *gotFinger = true if
			// IRQDATA_FINGER_ON was seen.
	virtual	status_t			WaitForFingerOn(bool* gotFinger);

			// Switches to capture mode and reads one raw image struct via
			// the bulk endpoint. buffer must be at least
			// Uru4000::kImageStructSize bytes. Returns the actual byte
			// count transferred (like BulkTransfer) or a negative error.
	virtual	ssize_t				CaptureImage(void* buffer, size_t size);

			// Arms for finger-off and blocks on exactly one interrupt
			// read, expected to be IRQDATA_FINGER_OFF.
	virtual	status_t			WaitForFingerOff(bool* gotRelease);

			// Powers the sensor down (MODE_OFF). Safe to call multiple
			// times / when not armed.
	virtual	void				Deactivate();

	virtual	int32				ImageWidth() const
									{ return Uru4000::kImageWidth; }
	virtual	int32				ImageHeight() const
									{ return Uru4000::kImageHeight; }
	virtual	size_t				ImageHeaderSize() const
									{ return Uru4000::kImageHeaderSize; }
	virtual	size_t				CaptureBufferSize() const
									{ return Uru4000::kImageStructSize; }

private:
			ssize_t				ReadRegs(uint16 reg, uint8* buf, uint16 len);
			ssize_t				WriteRegs(uint16 reg, const uint8* buf,
									uint16 len);
			status_t			WriteReg(uint16 reg, uint8 value);
			status_t			ReadHwstat(uint8* out);

			// Blocks for exactly one interrupt packet and returns the
			// parsed big-endian 16-bit type, or a negative status_t.
			int32				ReadOneIrq();

			BUSBDevice*			fDevice;
			const BUSBInterface* fInterface;
			const BUSBEndpoint* fIntrEndpoint;
			const BUSBEndpoint* fBulkEndpoint;
};


#endif // URU4000_DEVICE_H
