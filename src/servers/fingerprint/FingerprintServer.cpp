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
#include <File.h>
#include <FindDirectory.h>
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
	fLastSequenceMatchTime(0),
	fScreensaverUnlockEnabled(false),
	fWatcher(NULL)
{
	fDatabase.Init();
	fEscrowStore.Init();
	_LoadSettings();
}


FingerprintServer::~FingerprintServer()
{
	// Stop() must be called explicitly, before delete, while fWatcher is
	// still fully a FingerprintUSBWatcher. BUSBRoster::~BUSBRoster() also
	// calls Stop() as a safety net, but by the time that runs (as part of
	// the base-class destructor sub-object), the vtable has already been
	// rewound to plain BUSBRoster - whose DeviceAdded()/DeviceRemoved() are
	// pure virtual. If a device is still attached at that point, Stop()
	// tears down its WatchedEntry, which calls the (now pure virtual)
	// DeviceRemoved() and aborts with "pure virtual method called". This is
	// why quitting while the reader is plugged in crashed every time.
	if (fWatcher != NULL)
		fWatcher->Stop();
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
		case B_FP_SET_SEQUENCE_ESCROW:
			_HandleSetSequenceEscrow(message);
			break;
		case B_FP_CLEAR_SEQUENCE_ESCROW:
			_HandleClearSequenceEscrow(message);
			break;
		case B_FP_SEQUENCE_VERIFY_START:
			_HandleSequenceVerifyStart(message);
			break;
		case B_FP_GET_ESCROW:
			_HandleGetEscrow(message);
			break;
		case B_FP_SET_SCREENSAVER_UNLOCK:
			_HandleSetScreensaverUnlock(message);
			break;
		case B_FP_GET_SCREENSAVER_UNLOCK:
			_HandleGetScreensaverUnlock(message);
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
		if (status == B_OK) {
			// If the deleted finger was part of the active sequence
			// escrow, invalidate it now rather than leaving it around
			// stale - otherwise a later enrollment reusing that same
			// fingerId (FingerprintDatabase hands out the lowest free id,
			// so this is a real possibility after deleting everything)
			// could silently satisfy a sequence position it was never
			// actually authorized for.
			int32 storedSequence[EscrowStore::kSequenceLength];
			uint8 key[32];
			if (fEscrowStore.Get(storedSequence, key, sizeof(key)) == B_OK) {
				for (int32 i = 0; i < EscrowStore::kSequenceLength; i++) {
					if (storedSequence[i] == fingerId) {
						fEscrowStore.Clear();
						break;
					}
				}
			}
		}
	}

	BMessage reply;
	reply.AddInt32("error", status);
	message->SendReply(&reply);
}


void
FingerprintServer::_HandleSetSequenceEscrow(BMessage* message)
{
	int32 sequence[EscrowStore::kSequenceLength];
	const void* key;
	ssize_t keyLength;
	status_t status = B_BAD_VALUE;

	bool haveSequence = true;
	for (int32 i = 0; i < EscrowStore::kSequenceLength; i++) {
		if (message->FindInt32("sequence", i, &sequence[i]) != B_OK) {
			haveSequence = false;
			break;
		}
	}

	if (haveSequence
		&& message->FindData("key", B_RAW_TYPE, &key, &keyLength) == B_OK
		&& keyLength == 32) {
		status = fEscrowStore.Set(sequence, (const uint8*)key, 32);
	}

	BMessage reply;
	reply.AddInt32("error", status);
	message->SendReply(&reply);
}


void
FingerprintServer::_HandleClearSequenceEscrow(BMessage* message)
{
	status_t status = fEscrowStore.Clear();

	BMessage reply;
	reply.AddInt32("error", status);
	message->SendReply(&reply);
}


void
FingerprintServer::_HandleGetEscrow(BMessage* message)
{
	status_t status;
	uint8 key[32];

	bool fresh = (system_time() - fLastSequenceMatchTime) < kEscrowFreshness;
	if (!fresh) {
		status = B_NOT_ALLOWED;
	} else {
		int32 storedSequence[EscrowStore::kSequenceLength];
		status = fEscrowStore.Get(storedSequence, key, sizeof(key));
	}

	BMessage reply;
	reply.AddInt32("error", status);
	if (status == B_OK)
		reply.AddData("key", B_RAW_TYPE, key, sizeof(key));
	message->SendReply(&reply);

	// one-shot: don't allow a second GET_ESCROW off the same match
	fLastSequenceMatchTime = 0;
}


void
FingerprintServer::_HandleSetScreensaverUnlock(BMessage* message)
{
	bool enabled = false;
	message->FindBool("enabled", &enabled);
	fScreensaverUnlockEnabled = enabled;
	status_t status = _SaveSettings();

	BMessage reply;
	reply.AddInt32("error", status);
	message->SendReply(&reply);
}


void
FingerprintServer::_HandleGetScreensaverUnlock(BMessage* message)
{
	BMessage reply;
	reply.AddBool("enabled", fScreensaverUnlockEnabled);
	reply.AddInt32("error", B_OK);
	message->SendReply(&reply);
}


status_t
FingerprintServer::_SettingsPath(BString* outPath)
{
	BPath path;
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &path);
	if (status != B_OK)
		return status;

	path.Append("fingerprint_server");
	status = create_directory(path.Path(), 0700);
	if (status != B_OK && status != B_FILE_EXISTS)
		return status;

	path.Append("settings");
	*outPath = path.Path();
	return B_OK;
}


void
FingerprintServer::_LoadSettings()
{
	BString path;
	if (_SettingsPath(&path) != B_OK)
		return;

	BFile file(path.String(), B_READ_ONLY);
	if (file.InitCheck() != B_OK)
		return;

	BMessage settings;
	if (settings.Unflatten(&file) != B_OK)
		return;

	settings.FindBool("screensaverUnlock", &fScreensaverUnlockEnabled);
}


status_t
FingerprintServer::_SaveSettings()
{
	BString path;
	status_t status = _SettingsPath(&path);
	if (status != B_OK)
		return status;

	BMessage settings;
	settings.AddBool("screensaverUnlock", fScreensaverUnlockEnabled);

	BFile file(path.String(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	status = file.InitCheck();
	if (status != B_OK)
		return status;

	return settings.Flatten(&file);
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


void
FingerprintServer::_HandleSequenceVerifyStart(BMessage* message)
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
	if (!fEscrowStore.HasEscrow()) {
		BMessage reply;
		reply.AddInt32("error", B_NO_INIT);
		message->SendReply(&reply);
		return;
	}

	fOperationInProgress = true;
	fCancelRequested = false;

	BMessage* detached = DetachCurrentMessage();

	struct SequenceVerifyArgs {
		FingerprintServer* server;
		BMessage* request;
	};
	SequenceVerifyArgs* args = new SequenceVerifyArgs { this, detached };

	fWorkerThread = spawn_thread(_SequenceVerifyThreadEntry, "fp_seq_verify",
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


status_t
FingerprintServer::_SequenceVerifyThreadEntry(void* data)
{
	struct SequenceVerifyArgs {
		FingerprintServer* server;
		BMessage* request;
	};
	SequenceVerifyArgs* args = (SequenceVerifyArgs*)data;
	args->server->_SequenceVerifyThread(args->request);
	delete args;
	return B_OK;
}


status_t
FingerprintServer::_CaptureAndIdentifyOneFinger(FingerprintDevice* device,
	int32* outFingerId, int32* outScore)
{
	*outFingerId = -1;
	*outScore = -1000;

	bool gotFinger = false;
	status_t status = device->WaitForFingerOn(&gotFinger);
	if (status != B_OK)
		return status;
	if (!gotFinger)
		return B_ERROR;

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
		status = fDatabase.Match(probe, imageWidth, imageHeight, outFingerId,
			outScore);
		delete[] probe;
	} else {
		status = B_IO_ERROR;
	}
	delete[] buffer;

	bool gotRelease = false;
	device->WaitForFingerOff(&gotRelease);

	// B_NAME_NOT_FOUND from Match() just means "no match" - not a capture
	// error, so callers can tell the two apart via *outFingerId (-1) rather
	// than treating a plain non-match as a hard failure.
	if (status == B_NAME_NOT_FOUND)
		return B_OK;
	return status;
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

	if (status == B_OK && !fCancelRequested)
		status = _CaptureAndIdentifyOneFinger(device, &fingerId, &score);

	if (device != NULL)
		device->Deactivate();
	delete device;

	BMessage reply;
	reply.AddInt32("fingerId", fingerId);
	reply.AddInt32("score", score);
	reply.AddInt32("error", (status == B_OK && fingerId < 0)
		? (status_t)B_NAME_NOT_FOUND : status);
	request->SendReply(&reply);
	delete request;

	fOperationInProgress = false;
}


void
FingerprintServer::_SequenceVerifyThread(BMessage* request)
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

	int32 touched[EscrowStore::kSequenceLength];
	for (int32 i = 0; i < EscrowStore::kSequenceLength; i++)
		touched[i] = -1;

	// Collect-all-then-check: keep taking touches for the whole sequence
	// even if an earlier one didn't match anything, per the user's own
	// choice (no early-abort feedback) - only the final comparison below
	// decides success.
	for (int32 i = 0; i < EscrowStore::kSequenceLength
			&& status == B_OK && !fCancelRequested; i++) {
		int32 score = -1000;
		status = _CaptureAndIdentifyOneFinger(device, &touched[i], &score);
		fprintf(stderr, "[SequenceVerify] touch %d: fingerId=%d score=%d "
			"status=%s\n", (int)i, (int)touched[i], (int)score,
			strerror(status));
	}

	if (device != NULL)
		device->Deactivate();
	delete device;

	bool matched = false;
	if (status == B_OK) {
		int32 storedSequence[EscrowStore::kSequenceLength];
		uint8 key[32];
		if (fEscrowStore.Get(storedSequence, key, sizeof(key)) == B_OK) {
			fprintf(stderr, "[SequenceVerify] stored sequence: "
				"%d %d %d %d\n", (int)storedSequence[0],
				(int)storedSequence[1], (int)storedSequence[2],
				(int)storedSequence[3]);
			matched = true;
			for (int32 i = 0; i < EscrowStore::kSequenceLength; i++) {
				if (touched[i] != storedSequence[i]) {
					matched = false;
					break;
				}
			}
		} else {
			fprintf(stderr, "[SequenceVerify] fEscrowStore.Get() failed\n");
		}
	}
	fprintf(stderr, "[SequenceVerify] matched=%d\n", matched);

	if (matched)
		fLastSequenceMatchTime = system_time();

	BMessage reply;
	reply.AddInt32("error", matched
		? (status_t)B_OK
		: (status == B_OK ? (status_t)B_NAME_NOT_FOUND : status));
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
