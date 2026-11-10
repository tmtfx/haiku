/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include "Uru4000Device.h"

#include <stdio.h>
#include <string.h>

#include <new>

#include <OS.h>

#define FP_LOG(fmt...) do { fprintf(stderr, "[Uru4000Device] " fmt); \
	fflush(stderr); } while (0)


using namespace Uru4000;


Uru4000Device::Uru4000Device()
	:
	fDevice(NULL),
	fInterface(NULL),
	fIntrEndpoint(NULL),
	fBulkEndpoint(NULL)
{
}


Uru4000Device::~Uru4000Device()
{
	Close();
}


status_t
Uru4000Device::Open(const char* path)
{
	Close();

	fDevice = new(std::nothrow) BUSBDevice(path);
	if (fDevice == NULL)
		return B_NO_MEMORY;

	status_t status = fDevice->InitCheck();
	if (status != B_OK) {
		Close();
		return status;
	}

	const BUSBConfiguration* config = fDevice->ActiveConfiguration();
	if (config == NULL)
		config = fDevice->ConfigurationAt(0);
	if (config == NULL || config->CountInterfaces() == 0) {
		Close();
		return B_ERROR;
	}

	fInterface = config->InterfaceAt(0);
	if (fInterface == NULL) {
		Close();
		return B_ERROR;
	}

	for (uint32 i = 0; i < fInterface->CountEndpoints(); i++) {
		const BUSBEndpoint* endpoint = fInterface->EndpointAt(i);
		if (endpoint == NULL || !endpoint->IsInput())
			continue;
		if (endpoint->IsInterrupt() && fIntrEndpoint == NULL)
			fIntrEndpoint = endpoint;
		if (endpoint->IsBulk() && fBulkEndpoint == NULL)
			fBulkEndpoint = endpoint;
	}

	if (fIntrEndpoint == NULL || fBulkEndpoint == NULL) {
		Close();
		return B_ERROR;
	}

	return B_OK;
}


void
Uru4000Device::Close()
{
	delete fDevice;
	fDevice = NULL;
	fInterface = NULL;
	fIntrEndpoint = NULL;
	fBulkEndpoint = NULL;
}


uint16
Uru4000Device::VendorID() const
{
	return fDevice != NULL ? fDevice->VendorID() : 0;
}


uint16
Uru4000Device::ProductID() const
{
	return fDevice != NULL ? fDevice->ProductID() : 0;
}


ssize_t
Uru4000Device::ReadRegs(uint16 reg, uint8* buf, uint16 len)
{
	return fDevice->ControlTransfer(kCtrlIn, kVendorRequest, reg, 0, len, buf);
}


ssize_t
Uru4000Device::WriteRegs(uint16 reg, const uint8* buf, uint16 len)
{
	return fDevice->ControlTransfer(kCtrlOut, kVendorRequest, reg, 0, len,
		(void*)buf);
}


status_t
Uru4000Device::WriteReg(uint16 reg, uint8 value)
{
	ssize_t written = WriteRegs(reg, &value, 1);
	return written == 1 ? B_OK : (status_t)written;
}


status_t
Uru4000Device::ReadHwstat(uint8* out)
{
	ssize_t result = ReadRegs(REG_HWSTAT, out, 1);
	return result == 1 ? B_OK : (status_t)result;
}


int32
Uru4000Device::ReadOneIrq()
{
	uint8 buffer[64];
	ssize_t result = fIntrEndpoint->InterruptTransfer(buffer, sizeof(buffer));
	if (result < 0)
		return (int32)result;
	if (result < 2)
		return -1;
	return ((int32)buffer[0] << 8) | buffer[1];
}


status_t
Uru4000Device::Activate()
{
	if (!IsOpen())
		return B_NO_INIT;

	uint8 hwstat = 0;
	status_t status = ReadHwstat(&hwstat);
	if (status != B_OK)
		return status;

	if ((hwstat & 0x84) == 0x84) {
		// Device power got confused by a previous abrupt shutdown; mask
		// off the high bits and wait for it to report bit 0x1 again.
		WriteReg(REG_HWSTAT, hwstat & 0x0f);
		bool rebooted = false;
		for (int i = 0; i < 100; i++) {
			if (ReadHwstat(&hwstat) != B_OK)
				break;
			if (hwstat & 0x1) {
				rebooted = true;
				break;
			}
			snooze(10000);
		}
		if (!rebooted)
			return B_TIMED_OUT;
	}

	if ((hwstat & 0x80) == 0) {
		WriteReg(REG_HWSTAT, hwstat | 0x80);
		ReadHwstat(&hwstat);
	}

	uint8 hwstatLocal = hwstat & 0x0f;
	bool poweredUp = false;
	for (int i = 0; i < 100; i++) {
		WriteReg(REG_HWSTAT, hwstatLocal);
		if (ReadHwstat(&hwstat) != B_OK)
			break;
		if ((hwstat & 0x80) == 0) {
			poweredUp = true;
			break;
		}
		snooze(10000);
	}
	if (!poweredUp)
		return B_TIMED_OUT;

	// Safe to do exactly one blocking interrupt read here: no other
	// transfer is in flight. Expected type is IRQDATA_SCANPWR_ON, but we
	// don't hard-fail if something else (or nothing useful) comes back -
	// the device is already powered per HWSTAT at this point.
	ReadOneIrq();

	return B_OK;
}


status_t
Uru4000Device::WaitForFingerOn(bool* gotFinger)
{
	*gotFinger = false;
	if (!IsOpen())
		return B_NO_INIT;

	FP_LOG("WaitForFingerOn: writing MODE_AWAIT_FINGER_ON\n");
	status_t status = WriteReg(REG_MODE, MODE_AWAIT_FINGER_ON);
	FP_LOG("WaitForFingerOn: write result=%s\n", strerror(status));
	if (status != B_OK)
		return status;

	// The sensor can report spurious/duplicate interrupts around a touch
	// (observed in practice: a single quick touch can produce more than
	// one FINGER_ON before the expected FINGER_OFF). Keep reading and
	// ignore anything that isn't FINGER_ON, up to a bounded number of
	// packets, rather than treating the first unexpected type as fatal.
	for (int attempt = 0; attempt < 10; attempt++) {
		FP_LOG("WaitForFingerOn: blocking on interrupt read (attempt %d)...\n",
			attempt);
		int32 type = ReadOneIrq();
		FP_LOG("WaitForFingerOn: got type=0x%04lx\n", (long)type);
		if (type < 0)
			return (status_t)type;
		if (type == IRQDATA_FINGER_ON) {
			*gotFinger = true;
			return B_OK;
		}
		// anything else (duplicate scanpwr_on, stray finger_off, death):
		// ignore and keep waiting.
	}

	return B_TIMED_OUT;
}


ssize_t
Uru4000Device::CaptureImage(void* buffer, size_t size)
{
	if (!IsOpen())
		return B_NO_INIT;
	if (size < kImageStructSize)
		return B_BAD_VALUE;

	FP_LOG("CaptureImage: writing MODE_CAPTURE\n");
	status_t status = WriteReg(REG_MODE, MODE_CAPTURE);
	FP_LOG("CaptureImage: write result=%s\n", strerror(status));
	if (status != B_OK)
		return status;

	FP_LOG("CaptureImage: blocking on bulk read...\n");
	ssize_t n = fBulkEndpoint->BulkTransfer(buffer, kImageStructSize);
	FP_LOG("CaptureImage: bulk read returned %ld\n", (long)n);

	FP_LOG("CaptureImage: bulk endpoint stalled=%d\n",
		fBulkEndpoint->IsStalled());
	if (fBulkEndpoint->IsStalled()) {
		status_t clearStatus = fBulkEndpoint->ClearStall();
		FP_LOG("CaptureImage: ClearStall result=%s\n", strerror(clearStatus));
	}

	return n;
}


status_t
Uru4000Device::WaitForFingerOff(bool* gotRelease)
{
	*gotRelease = false;
	if (!IsOpen())
		return B_NO_INIT;

	FP_LOG("WaitForFingerOff: writing MODE_AWAIT_FINGER_OFF\n");
	status_t status = WriteReg(REG_MODE, MODE_AWAIT_FINGER_OFF);
	FP_LOG("WaitForFingerOff: write result=%s\n", strerror(status));
	if (status != B_OK)
		return status;

	// Same rationale as WaitForFingerOn(): tolerate stray/duplicate
	// interrupts (e.g. a repeated FINGER_ON from sensor bounce) instead of
	// giving up on the first unexpected packet.
	for (int attempt = 0; attempt < 10; attempt++) {
		FP_LOG("WaitForFingerOff: blocking on interrupt read (attempt %d)...\n",
			attempt);
		int32 type = ReadOneIrq();
		FP_LOG("WaitForFingerOff: got type=0x%04lx\n", (long)type);
		if (type < 0)
			return (status_t)type;
		if (type == IRQDATA_FINGER_OFF) {
			*gotRelease = true;
			return B_OK;
		}
	}

	return B_TIMED_OUT;
}


void
Uru4000Device::Deactivate()
{
	if (!IsOpen())
		return;

	status_t status = WriteReg(REG_MODE, MODE_OFF);
	FP_LOG("Deactivate: writing MODE_OFF, result=%s\n", strerror(status));

	// MODE_OFF alone was observed (on real hardware) to leave the sensor
	// LED at the same dim "waiting" brightness rather than extinguishing
	// it. HWSTAT bit 0x80 is documented (see the comment above
	// Activate()'s powerup loop, ported from libfprint) as the device's
	// low-power bit, which Activate() clears to wake it up - so set it
	// again here as the symmetric power-down step.
	uint8 hwstat = 0;
	if (ReadHwstat(&hwstat) == B_OK && (hwstat & 0x80) == 0) {
		status = WriteReg(REG_HWSTAT, hwstat | 0x80);
		FP_LOG("Deactivate: setting HWSTAT low-power bit, result=%s\n",
			strerror(status));
	}
}
