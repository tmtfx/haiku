/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include "FingerprintServer.h"

#include <stdio.h>
#include <string.h>

#include <new>

#include <Directory.h>
#include <Entry.h>
#include <Message.h>
#include <OS.h>
#include <Path.h>

#include "FingerprintDeviceRegistry.h"
#include "defs.h"


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
	if (!FingerprintDeviceRegistry::Matches(device->VendorID(),
			device->ProductID())) {
		return B_ERROR; // not interested, BUSBRoster deletes the object
	}

	fOwner->SetDevicePresent(device->VendorID(), device->ProductID());
	return B_OK; // keep the object alive, we'll get DeviceRemoved() later
}


void
FingerprintUSBWatcher::DeviceRemoved(BUSBDevice* device)
{
	fOwner->ClearDevicePresent();
}


// #pragma mark - FingerprintServer


FingerprintServer::FingerprintServer()
	:
	BApplication(FINGERPRINT_SERVER_SIGNATURE),
	fVendorID(0),
	fProductID(0),
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
FingerprintServer::SetDevicePresent(uint16 vendorID, uint16 productID)
{
	fVendorID = vendorID;
	fProductID = productID;
}


void
FingerprintServer::ClearDevicePresent()
{
	fVendorID = 0;
	fProductID = 0;
}


status_t
FingerprintServer::_FindDevicePath(BString* outPath)
{
	if (fVendorID == 0)
		return B_NO_INIT;

	BDirectory busDir("/dev/bus/usb");
	if (busDir.InitCheck() != B_OK)
		return B_NO_INIT;

	BEntry hubEntry;
	while (busDir.GetNextEntry(&hubEntry) == B_OK) {
		BPath hubPath;
		if (hubEntry.GetPath(&hubPath) != B_OK || !hubEntry.IsDirectory())
			continue;

		BDirectory hubDir(hubPath.Path());
		BEntry deviceEntry;
		while (hubDir.GetNextEntry(&deviceEntry) == B_OK) {
			char name[B_FILE_NAME_LENGTH];
			if (deviceEntry.GetName(name) != B_OK)
				continue;
			if (strcmp(name, "raw") == 0)
				continue; // pseudo-entry, ioctl-only, not a real device

			BPath devicePath;
			if (deviceEntry.GetPath(&devicePath) != B_OK)
				continue;

			BUSBDevice device(devicePath.Path());
			if (device.InitCheck() != B_OK)
				continue;

			if (device.VendorID() == fVendorID
				&& device.ProductID() == fProductID) {
				*outPath = devicePath.Path();
				return B_OK;
			}
		}
	}

	return B_ENTRY_NOT_FOUND;
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
	if (fVendorID == 0) {
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
	if (fVendorID == 0) {
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
	FingerprintDevice* device = FingerprintDeviceRegistry::Create(fVendorID,
		fProductID);
	status_t status = device != NULL ? B_OK : B_NO_INIT;

	BString path;
	if (status == B_OK)
		status = _FindDevicePath(&path);
	if (status == B_OK)
		status = device->Open(path.String());
	if (status == B_OK)
		status = device->Activate();

	uint8* samples[kEnrollSampleCount];
	for (int32 i = 0; i < kEnrollSampleCount; i++)
		samples[i] = NULL;

	int32 imageWidth = device != NULL ? device->ImageWidth() : 0;
	int32 imageHeight = device != NULL ? device->ImageHeight() : 0;

	int32 captured = 0;
	for (; captured < kEnrollSampleCount && status == B_OK
			&& !fCancelRequested; captured++) {
		bool gotFinger = false;
		status = device->WaitForFingerOn(&gotFinger);
		if (status != B_OK || !gotFinger)
			break;

		size_t bufferSize = device->CaptureBufferSize();
		size_t headerSize = device->ImageHeaderSize();
		uint8* buffer = new(std::nothrow) uint8[bufferSize];
		ssize_t n = device->CaptureImage(buffer, bufferSize);
		// A short transfer is the NORMAL case here (many sensors report
		// fewer lines/bytes than the max almost every capture) - only
		// treat it as an error if we didn't even get the fixed-size
		// header.
		if (n < (ssize_t)headerSize) {
			delete[] buffer;
			status = B_IO_ERROR;
			break;
		}
		size_t pixelBytes = (size_t)n - headerSize;
		size_t imageBytes = (size_t)(imageWidth * imageHeight);
		if (pixelBytes > imageBytes)
			pixelBytes = imageBytes;

		uint8* image = new(std::nothrow) uint8[imageBytes];
		memset(image, 0, imageBytes);
		memcpy(image, buffer + headerSize, pixelBytes);
		delete[] buffer;
		samples[captured] = image;

		bool gotRelease = false;
		status_t offStatus = device->WaitForFingerOff(&gotRelease);
		if (offStatus != B_OK) {
			status = offStatus;
			captured++; // this sample was still captured successfully
			break;
		}
	}

	if (device != NULL)
		device->Deactivate();

	int32 fingerId = -1;
	if (status == B_OK && captured == kEnrollSampleCount) {
		status = fDatabase.SaveEnrollment(label.String(),
			(const uint8* const*)samples, kEnrollSampleCount, imageWidth,
			imageHeight, &fingerId);
	} else if (status == B_OK) {
		status = fCancelRequested ? B_CANCELED : B_ERROR;
	}

	for (int32 i = 0; i < kEnrollSampleCount; i++)
		delete[] samples[i];
	delete device;

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
	FingerprintDevice* device = FingerprintDeviceRegistry::Create(fVendorID,
		fProductID);
	status_t status = device != NULL ? B_OK : B_NO_INIT;

	BString path;
	if (status == B_OK)
		status = _FindDevicePath(&path);
	if (status == B_OK)
		status = device->Open(path.String());
	if (status == B_OK)
		status = device->Activate();

	int32 fingerId = -1;
	int32 score = -1000;

	if (status == B_OK && !fCancelRequested) {
		bool gotFinger = false;
		status = device->WaitForFingerOn(&gotFinger);
		if (status == B_OK && gotFinger) {
			size_t bufferSize = device->CaptureBufferSize();
			size_t headerSize = device->ImageHeaderSize();
			int32 imageWidth = device->ImageWidth();
			int32 imageHeight = device->ImageHeight();

			uint8* buffer = new(std::nothrow) uint8[bufferSize];
			ssize_t n = device->CaptureImage(buffer, bufferSize);
			if (n >= (ssize_t)headerSize) {
				size_t pixelBytes = (size_t)n - headerSize;
				size_t imageBytes = (size_t)(imageWidth * imageHeight);
				if (pixelBytes > imageBytes)
					pixelBytes = imageBytes;
				uint8* probe = new(std::nothrow) uint8[imageBytes];
				memset(probe, 0, imageBytes);
				memcpy(probe, buffer + headerSize, pixelBytes);
				status = fDatabase.Match(probe, imageWidth, imageHeight,
					&fingerId, &score);
				delete[] probe;
			} else {
				status = B_IO_ERROR;
			}
			delete[] buffer;

			bool gotRelease = false;
			device->WaitForFingerOff(&gotRelease);
		}
	}

	if (device != NULL)
		device->Deactivate();
	delete device;

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
