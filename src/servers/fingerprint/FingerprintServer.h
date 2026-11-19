/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef FINGERPRINT_SERVER_H
#define FINGERPRINT_SERVER_H


#include <Application.h>
#include <Messenger.h>
#include <String.h>
#include <USBKit.h>

#include "EscrowStore.h"
#include "FingerprintDatabase.h"
#include "FingerprintDevice.h"


// Watches the USB bus for the fingerprint reader, as a tiny subclass of
// BUSBRoster (node-monitor based - unrelated to, and unaffected by, the
// per-device transfer serialization issue that rules out a persistent IRQ
// polling thread. This only watches for the device file appearing).
class FingerprintUSBWatcher : public BUSBRoster {
public:
								FingerprintUSBWatcher(class FingerprintServer* owner);

	virtual	status_t			DeviceAdded(BUSBDevice* device);
	virtual	void				DeviceRemoved(BUSBDevice* device);

private:
			class FingerprintServer* fOwner;
};


class FingerprintServer : public BApplication {
public:
								FingerprintServer();
	virtual						~FingerprintServer();

	virtual	void				MessageReceived(BMessage* message);
	virtual	void				ReadyToRun();

			// called by FingerprintUSBWatcher
			void				SetDevicePresent(uint16 vendorID,
									uint16 productID);
			void				ClearDevicePresent();

private:
			// Scans /dev/bus/usb for a currently-attached device matching
			// fVendorID/fProductID and returns its path. Done fresh at the
			// start of each enroll/verify rather than trusting a path
			// captured earlier, since a device's bus path can change
			// across a replug and nothing re-validates a cached one.
			status_t			_FindDevicePath(BString* outPath);

			void				_HandleEnrollStart(BMessage* message);
			void				_HandleEnrollCancel(BMessage* message);
			void				_HandleList(BMessage* message);
			void				_HandleDelete(BMessage* message);
			void				_HandleVerifyStart(BMessage* message);
			void				_HandleVerifyCancel(BMessage* message);
			void				_HandleSetSequenceEscrow(BMessage* message);
			void				_HandleClearSequenceEscrow(BMessage* message);
			void				_HandleSequenceVerifyStart(BMessage* message);
			void				_HandleGetEscrow(BMessage* message);
			void				_HandleSetScreensaverUnlock(
									BMessage* message);
			void				_HandleGetScreensaverUnlock(
									BMessage* message);

			status_t			_SettingsPath(BString* outPath);
			void				_LoadSettings();
			status_t			_SaveSettings();

	static	status_t			_EnrollThreadEntry(void* data);
	static	status_t			_VerifyThreadEntry(void* data);
	static	status_t			_SequenceVerifyThreadEntry(void* data);

			// Take ownership of the client's request via
			// DetachCurrentMessage() and reply to it only once the whole
			// operation completes - see the IPC design note above
			// _HandleEnrollStart() in the .cpp for why (repeated
			// fire-and-forget BMessenger::SendMessage() push notifications
			// from a worker thread reliably broke after the first one on
			// real hardware; the synchronous request/reply pattern never
			// did).
			void				_EnrollThread(BMessage* request,
									const BString& label);
			void				_VerifyThread(BMessage* request);
			void				_SequenceVerifyThread(BMessage* request);

			// Shared by _VerifyThread and _SequenceVerifyThread: arms the
			// already-open device for one touch, captures, identifies via
			// FingerprintDatabase::Match(), then waits for release. Device
			// Open()/Activate()/Deactivate()/Close() are the caller's
			// responsibility - this only drives one touch cycle.
			status_t			_CaptureAndIdentifyOneFinger(
									FingerprintDevice* device,
									int32* outFingerId, int32* outScore);

			FingerprintDatabase	fDatabase;
			EscrowStore			fEscrowStore;

			uint16				fVendorID;		// 0 if no reader attached
			uint16				fProductID;
			bool				fOperationInProgress;
			volatile bool		fCancelRequested;
			thread_id			fWorkerThread;

			// Set only by a fully successful _SequenceVerifyThread (all 4
			// touches matched the stored sequence, in order); gates
			// _HandleGetEscrow exactly like the old single-finger
			// freshness check did, just no longer tied to one fingerId.
			bigtime_t			fLastSequenceMatchTime;

			bool				fScreensaverUnlockEnabled;

			FingerprintUSBWatcher* fWatcher;
};


#endif // FINGERPRINT_SERVER_H
