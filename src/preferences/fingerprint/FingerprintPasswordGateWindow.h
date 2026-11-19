/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef FINGERPRINT_PASSWORD_GATE_WINDOW_H
#define FINGERPRINT_PASSWORD_GATE_WINDOW_H


#include <Window.h>


class BButton;
class BStringView;
class BTextControl;


// Gates opening the Fingerprint preflet behind the master password, read
// and verified directly against the shadow file (salt+blake2b, same check
// keystore_server does) - deliberately NOT a full keystore_server session
// unlock, just the lightweight password check. Without this, anyone with
// access to the running session could freely enroll/delete fingerprint
// templates with no authentication at all, which (combined with the
// Fase 1e sequence-escrow feature) could let an already-unlocked session
// be hijacked into accepting a newly-enrolled, attacker-controlled finger
// sequence without ever having to know the real password.
class FingerprintPasswordGateWindow : public BWindow {
public:
	// Returns B_OK if the preflet should open: either no master password
	// is configured at all (no shadow file, or an intentionally empty
	// one), or the user typed the correct one. B_CANCELED otherwise - the
	// caller should quit without ever showing the main window.
	static	status_t			CheckAccess();

	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				QuitRequested();

private:
								FingerprintPasswordGateWindow(
									const uint8* salt, const uint8* hash);
	virtual						~FingerprintPasswordGateWindow();

			status_t			RequestAccess();

			uint8				fSalt[16];
			uint8				fHash[64];

			BTextControl*		fPassword;
			BStringView*		fStatus;
			BButton*			fUnlockButton;
			BButton*			fCancelButton;

			sem_id				fDoneSem;
			status_t			fResult;
};


#endif // FINGERPRINT_PASSWORD_GATE_WINDOW_H
