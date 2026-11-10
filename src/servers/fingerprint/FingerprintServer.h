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
#include "protocol/uru4000/Uru4000Device.h"


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
			void				SetDevicePath(const BString& path);
			void				ClearDevicePath();

private:
			void				_HandleEnrollStart(BMessage* message);
			void				_HandleEnrollCancel(BMessage* message);
			void				_HandleList(BMessage* message);
			void				_HandleDelete(BMessage* message);
			void				_HandleVerifyStart(BMessage* message);
			void				_HandleVerifyCancel(BMessage* message);
			void				_HandleSetEscrow(BMessage* message);
			void				_HandleGetEscrow(BMessage* message);

	static	status_t			_EnrollThreadEntry(void* data);
	static	status_t			_VerifyThreadEntry(void* data);

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

			FingerprintDatabase	fDatabase;
			EscrowStore			fEscrowStore;

			BString				fDevicePath;	// empty if no reader attached
			bool				fOperationInProgress;
			volatile bool		fCancelRequested;
			thread_id			fWorkerThread;

			int32				fLastMatchFingerId;
			bigtime_t			fLastMatchTime;

			FingerprintUSBWatcher* fWatcher;
};


#endif // FINGERPRINT_SERVER_H
