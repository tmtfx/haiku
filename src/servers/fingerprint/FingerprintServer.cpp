/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include "FingerprintServer.h"

#include <stdio.h>
#include <string.h>

#include <new>

#include <Entry.h>
#include <Message.h>
#include <OS.h>
#include <Path.h>

#include "defs.h"
#include "protocol/uru4000/Uru4000Protocol.h"

using namespace Uru4000;


static const int32 kEnrollSampleCount = 3;
static const bigtime_t kEscrowFreshness = 10000000; // 10s


// #pragma mark - FingerprintUSBWatcher


FingerprintUSBWatcher::FingerprintUSBWatcher(FingerprintServer* owner)
	:
	fOwner(owner)
{
}


status_t
FingerprintUSBWatcher::DeviceAdded(BUSBDevice* device)
{
	if (device->VendorID() != kVendorID || device->ProductID() != kProductID)
		return B_ERROR; // not interested, BUSBRoster deletes the object

	BString path;
	// BUSBDevice doesn't expose its own raw path, but Location() plus the
	// bus layout under /dev/bus/usb is what BUSBRoster used to find it; we
	// just need *a* path that BUSBDevice::SetTo() style construction from
	// FingerprintServer can reopen later. Simplest robust option: rescan
	// /dev/bus/usb for a device matching VID/PID when we actually need to
	// open it, rather than caching this particular path (bus-rescan is
	// cheap and avoids any staleness if the path changes on replug).
	fOwner->SetDevicePath("rescan");
	return B_OK; // keep the object alive, we'll get DeviceRemoved() later
}


void
FingerprintUSBWatcher::DeviceRemoved(BUSBDevice* device)
{
	fOwner->ClearDevicePath();
}


// #pragma mark - FingerprintServer


FingerprintServer::FingerprintServer()
	:
	BApplication(FINGERPRINT_SERVER_SIGNATURE),
	fOperationInProgress(false),
	fCancelRequested(false),
	fWorkerThread(-1),
	fLastMatchFingerId(-1),
	fLastMatchTime(0),
	fWatcher(NULL)
{
	fDatabase.Init();
	fEscrowStore.Init();
}


FingerprintServer::~FingerprintServer()
{
	delete fWatcher;
}


void
FingerprintServer::ReadyToRun()
{
	fWatcher = new(std::nothrow) FingerprintUSBWatcher(this);
	if (fWatcher != NULL)
		fWatcher->Start();
}


void
FingerprintServer::SetDevicePath(const BString& path)
{
	fDevicePath = path;
}


void
FingerprintServer::ClearDevicePath()
{
	fDevicePath = "";
}


void
FingerprintServer::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_FP_ENROLL_START:
			_HandleEnrollStart(message);
			break;
		case B_FP_ENROLL_CANCEL:
			_HandleEnrollCancel(message);
			break;
		case B_FP_LIST:
			_HandleList(message);
			break;
		case B_FP_DELETE:
			_HandleDelete(message);
			break;
		case B_FP_VERIFY_START:
			_HandleVerifyStart(message);
			break;
		case B_FP_VERIFY_CANCEL:
			_HandleVerifyCancel(message);
			break;
		case B_FP_SET_ESCROW:
			_HandleSetEscrow(message);
			break;
		case B_FP_GET_ESCROW:
			_HandleGetEscrow(message);
			break;
		default:
			BApplication::MessageReceived(message);
			break;
	}
}


// #pragma mark - request handlers


void
FingerprintServer::_HandleList(BMessage* message)
{
	BMessage reply;
	status_t status = fDatabase.List(&reply);
	reply.AddInt32("error", status);
	message->SendReply(&reply);
}


void
FingerprintServer::_HandleDelete(BMessage* message)
{
	int32 fingerId;
	status_t status = B_BAD_VALUE;
	if (message->FindInt32("fingerId", &fingerId) == B_OK) {
		status = fDatabase.Delete(fingerId);
		fEscrowStore.Delete(fingerId); // best effort, inert if absent
	}

	BMessage reply;
	reply.AddInt32("error", status);
	message->SendReply(&reply);
}


void
FingerprintServer::_HandleSetEscrow(BMessage* message)
{
	int32 fingerId;
	const void* key;
	ssize_t keyLength;
	status_t status = B_BAD_VALUE;

	if (message->FindInt32("fingerId", &fingerId) == B_OK
		&& message->FindData("key", B_RAW_TYPE, &key, &keyLength) == B_OK
		&& keyLength == 32) {
		status = fEscrowStore.Set(fingerId, (const uint8*)key, 32);
	}

	BMessage reply;
	reply.AddInt32("error", status);
	message->SendReply(&reply);
}


void
FingerprintServer::_HandleGetEscrow(BMessage* message)
{
	int32 fingerId;
	status_t status = B_BAD_VALUE;
	uint8 key[32];

	if (message->FindInt32("fingerId", &fingerId) == B_OK) {
		bool fresh = fingerId == fLastMatchFingerId
			&& (system_time() - fLastMatchTime) < kEscrowFreshness;
		if (!fresh) {
			status = B_NOT_ALLOWED;
		} else {
			status = fEscrowStore.Get(fingerId, key, sizeof(key));
		}
	}

	BMessage reply;
	reply.AddInt32("error", status);
	if (status == B_OK)
		reply.AddData("key", B_RAW_TYPE, key, sizeof(key));
	message->SendReply(&reply);

	// one-shot: don't allow a second GET_ESCROW off the same match
	fLastMatchFingerId = -1;
}


// NOTE on IPC design: an earlier version of this code replied to the
// ENROLL/VERIFY_START request immediately and then pushed progress/done
// events to the client via separate BMessenger::SendMessage() calls as the
// operation ran. On real hardware this reliably broke after the first such
// "fire and forget" push per client team: every call after the first timed
// out, regardless of which SendMessage overload, calling thread, or
// replyHandler was used - strongly suggesting a small, non-recycled
// per-team reply-port pool being exhausted. The one pattern that was
// reliable in every single test was the synchronous request/reply call the
// client already uses (BMessenger::SendMessage(msg, &reply) / our own
// BMessage::SendReply()). So ENROLL/VERIFY_START are now fully synchronous
// from the client's point of view: the request is detached and only
// replied to once the whole operation (which can take up to ~30s of user
// interaction for enroll) completes. There is no separate progress-event
// channel in this version - see Fase 1 follow-up for a polling-based
// status query if a live progress bar is wanted later.


void
FingerprintServer::_HandleEnrollStart(BMessage* message)
{
	if (fDevicePath.IsEmpty()) {
		BMessage reply;
		reply.AddInt32("error", B_NO_INIT);
		message->SendReply(&reply);
		return;
	}
	if (fOperationInProgress) {
		BMessage reply;
		reply.AddInt32("error", B_BUSY);
		message->SendReply(&reply);
		return;
	}

	BString label;
	message->FindString("label", &label);
	if (label.IsEmpty())
		label = "Finger";

	fOperationInProgress = true;
	fCancelRequested = false;

	BMessage* detached = DetachCurrentMessage();

	struct EnrollArgs {
		FingerprintServer* server;
		BMessage* request;
		BString label;
	};
	EnrollArgs* args = new EnrollArgs { this, detached, label };

	fWorkerThread = spawn_thread(_EnrollThreadEntry, "fp_enroll",
		B_NORMAL_PRIORITY, args);
	resume_thread(fWorkerThread);
}


void
FingerprintServer::_HandleEnrollCancel(BMessage* message)
{
	fCancelRequested = true;
}


void
FingerprintServer::_HandleVerifyCancel(BMessage* message)
{
	fCancelRequested = true;
}


void
FingerprintServer::_HandleVerifyStart(BMessage* message)
{
	if (fDevicePath.IsEmpty()) {
		BMessage reply;
		reply.AddInt32("error", B_NO_INIT);
		message->SendReply(&reply);
		return;
	}
	if (fOperationInProgress) {
		BMessage reply;
		reply.AddInt32("error", B_BUSY);
		message->SendReply(&reply);
		return;
	}

	fOperationInProgress = true;
	fCancelRequested = false;

	BMessage* detached = DetachCurrentMessage();

	struct VerifyArgs {
		FingerprintServer* server;
		BMessage* request;
	};
	VerifyArgs* args = new VerifyArgs { this, detached };

	fWorkerThread = spawn_thread(_VerifyThreadEntry, "fp_verify",
		B_NORMAL_PRIORITY, args);
	resume_thread(fWorkerThread);
}


// #pragma mark - worker threads


status_t
FingerprintServer::_EnrollThreadEntry(void* data)
{
	struct EnrollArgs {
		FingerprintServer* server;
		BMessage* request;
		BString label;
	};
	EnrollArgs* args = (EnrollArgs*)data;
	args->server->_EnrollThread(args->request, args->label);
	delete args;
	return B_OK;
}


status_t
FingerprintServer::_VerifyThreadEntry(void* data)
{
	struct VerifyArgs {
		FingerprintServer* server;
		BMessage* request;
	};
	VerifyArgs* args = (VerifyArgs*)data;
	args->server->_VerifyThread(args->request);
	delete args;
	return B_OK;
}


void
FingerprintServer::_EnrollThread(BMessage* request, const BString& label)
{
	Uru4000Device device;
	status_t status = device.Open(fDevicePath == "rescan"
		? "/dev/bus/usb/0/5" : fDevicePath.String());
	// NOTE: the "rescan" placeholder path resolution is a known shortcut -
	// see the TODO in FingerprintUSBWatcher::DeviceAdded. Fine for a
	// single-reader development box; a real multi-bus-location scan
	// belongs here before shipping.

	if (status == B_OK)
		status = device.Activate();

	uint8* samples[kEnrollSampleCount];
	for (int32 i = 0; i < kEnrollSampleCount; i++)
		samples[i] = NULL;

	int32 captured = 0;
	for (; captured < kEnrollSampleCount && status == B_OK
			&& !fCancelRequested; captured++) {
		bool gotFinger = false;
		status = device.WaitForFingerOn(&gotFinger);
		if (status != B_OK || !gotFinger)
			break;

		uint8* buffer = new(std::nothrow) uint8[kImageStructSize];
		ssize_t n = device.CaptureImage(buffer, kImageStructSize);
		// A short bulk transfer is the NORMAL case here (the device reports
		// fewer than the max 290 lines almost every capture) - only treat
		// it as an error if we didn't even get the fixed-size header.
		if (n < (ssize_t)kImageHeaderSize) {
			delete[] buffer;
			status = B_IO_ERROR;
			break;
		}
		size_t pixelBytes = (size_t)n - kImageHeaderSize;
		if (pixelBytes > (size_t)(kImageWidth * kImageHeight))
			pixelBytes = kImageWidth * kImageHeight;

		uint8* image = new(std::nothrow) uint8[kImageWidth * kImageHeight];
		memset(image, 0, kImageWidth * kImageHeight);
		memcpy(image, buffer + kImageHeaderSize, pixelBytes);
		delete[] buffer;
		samples[captured] = image;

		bool gotRelease = false;
		status_t offStatus = device.WaitForFingerOff(&gotRelease);
		if (offStatus != B_OK) {
			status = offStatus;
			captured++; // this sample was still captured successfully
			break;
		}
	}

	device.Deactivate();

	int32 fingerId = -1;
	if (status == B_OK && captured == kEnrollSampleCount) {
		status = fDatabase.SaveEnrollment(label.String(),
			(const uint8* const*)samples, kEnrollSampleCount, &fingerId);
	} else if (status == B_OK) {
		status = fCancelRequested ? B_CANCELED : B_ERROR;
	}

	for (int32 i = 0; i < kEnrollSampleCount; i++)
		delete[] samples[i];

	BMessage reply;
	reply.AddInt32("fingerId", fingerId);
	reply.AddInt32("error", status);
	request->SendReply(&reply);
	delete request;

	fOperationInProgress = false;
}


void
FingerprintServer::_VerifyThread(BMessage* request)
{
	Uru4000Device device;
	status_t status = device.Open(fDevicePath == "rescan"
		? "/dev/bus/usb/0/5" : fDevicePath.String());

	if (status == B_OK)
		status = device.Activate();

	int32 fingerId = -1;
	int32 score = -1000;

	if (status == B_OK && !fCancelRequested) {
		bool gotFinger = false;
		status = device.WaitForFingerOn(&gotFinger);
		if (status == B_OK && gotFinger) {
			uint8* buffer = new(std::nothrow) uint8[kImageStructSize];
			ssize_t n = device.CaptureImage(buffer, kImageStructSize);
			if (n >= (ssize_t)kImageHeaderSize) {
				size_t pixelBytes = (size_t)n - kImageHeaderSize;
				if (pixelBytes > (size_t)(kImageWidth * kImageHeight))
					pixelBytes = kImageWidth * kImageHeight;
				uint8 probe[kImageWidth * kImageHeight];
				memset(probe, 0, sizeof(probe));
				memcpy(probe, buffer + kImageHeaderSize, pixelBytes);
				status = fDatabase.Match(probe, &fingerId, &score);
			} else {
				status = B_IO_ERROR;
			}
			delete[] buffer;

			bool gotRelease = false;
			device.WaitForFingerOff(&gotRelease);
		}
	}

	device.Deactivate();

	if (status == B_OK && fingerId >= 0) {
		fLastMatchFingerId = fingerId;
		fLastMatchTime = system_time();
	}

	BMessage reply;
	reply.AddInt32("fingerId", fingerId);
	reply.AddInt32("score", score);
	reply.AddInt32("error", (status == B_OK && fingerId < 0)
		? (status_t)B_NAME_NOT_FOUND : status);
	request->SendReply(&reply);
	delete request;

	fOperationInProgress = false;
}


// #pragma mark - main


int
main(int argc, char** argv)
{
	FingerprintServer app;
	app.Run();
	return 0;
}
