/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef FINGERPRINT_ACTIVATION_WINDOW_H
#define FINGERPRINT_ACTIVATION_WINDOW_H


#include <Window.h>


class BButton;
class BListView;
class BStringView;


// Modal dialog for setting up fingerprint-unlock of the master password
// (Fase 1e revisionata): picks which removable USB stick to dedicate as
// the escrow medium, and an ordered 4-finger sequence from the already
// enrolled fingers. Synchronous RequestActivation(), modeled on the same
// semaphore-blocking pattern as MasterPasswordRequestWindow.
class FingerprintActivationWindow : public BWindow {
public:
								FingerprintActivationWindow();
	virtual						~FingerprintActivationWindow();

	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				QuitRequested();

			// Blocks the calling thread (not this window's own looper
			// thread) until the user confirms or cancels. On B_OK,
			// outSequence[4] and *outDevice are filled in.
			status_t			RequestActivation(int32* outSequence,
									dev_t* outDevice);

private:
			void				_RefreshVolumes();
			void				_RefreshFingers();
			void				_AddSelectedToSequence();
			void				_ResetSequence();
			void				_UpdateStatus();
			void				_UpdateConfirmEnabled();

			BListView*			fVolumeList;
			BListView*			fFingerList;
			BStringView*		fSequenceStatus;
			BButton*			fAddButton;
			BButton*			fResetButton;
			BButton*			fConfirmButton;
			BButton*			fCancelButton;

			int32				fSequence[4];
			int32				fSequenceLength;

			sem_id				fDoneSem;
			status_t			fResult;
};


#endif // FINGERPRINT_ACTIVATION_WINDOW_H
