/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include "FingerprintPasswordGateWindow.h"

#include <string.h>

#include <Button.h>
#include <Catalog.h>
#include <File.h>
#include <FindDirectory.h>
#include <LayoutBuilder.h>
#include <Message.h>
#include <Path.h>
#include <StringView.h>
#include <TextControl.h>

#include <crypto/BCrypto.h>
#include <crypto/BCryptoDefs.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "FingerprintPasswordGateWindow"


enum {
	kMsgUnlock	= 'fgUn',
	kMsgCancel	= 'fgCl',
};


static status_t
ReadShadow(uint8* outSalt, uint8* outHash)
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK)
		return B_ERROR;
	path.Append("shadow");

	BFile file(path.Path(), B_READ_ONLY);
	if (file.InitCheck() != B_OK)
		return file.InitCheck();

	BMessage shadow;
	if (shadow.Unflatten(&file) != B_OK)
		return B_ERROR;

	const void* salt = NULL;
	ssize_t saltLen = 0;
	const void* hash = NULL;
	ssize_t hashLen = 0;
	if (shadow.FindData("salt", B_RAW_TYPE, &salt, &saltLen) != B_OK
		|| saltLen != 16) {
		return B_BAD_VALUE;
	}
	if (shadow.FindData("hash", B_RAW_TYPE, &hash, &hashLen) != B_OK
		|| hashLen != 64) {
		return B_BAD_VALUE;
	}

	memcpy(outSalt, salt, 16);
	memcpy(outHash, hash, 64);
	return B_OK;
}


static bool
PasswordMatches(const char* password, const uint8* salt, const uint8* hash)
{
	size_t passLen = strlen(password);
	size_t inputLen = passLen + 16;
	uint8* input = new(std::nothrow) uint8[inputLen];
	if (input == NULL)
		return false;

	memcpy(input, password, passLen);
	memcpy(input + passLen, salt, 16);

	BCrypto crypto;
	uint8 computed[64];
	bool matches = crypto.InitCheck() == B_OK
		&& crypto.Digest(B_CRYPTO_BLAKE2B, input, inputLen, computed) == B_OK
		&& memcmp(computed, hash, 64) == 0;

	memset(input, 0, inputLen);
	delete[] input;
	return matches;
}


status_t
FingerprintPasswordGateWindow::CheckAccess()
{
	uint8 salt[16];
	uint8 hash[64];
	if (ReadShadow(salt, hash) != B_OK) {
		// No master password configured at all on this install - nothing
		// to gate.
		return B_OK;
	}

	// Intentionally-empty password (same convention as keystore_server's
	// own _GetOrAskSessionPassword): the shadow hash is just blake2b(salt)
	// alone. No real protection either way, so don't bother prompting.
	BCrypto crypto;
	uint8 emptyHash[64];
	if (crypto.InitCheck() == B_OK
		&& crypto.Digest(B_CRYPTO_BLAKE2B, salt, 16, emptyHash) == B_OK
		&& memcmp(emptyHash, hash, 64) == 0) {
		return B_OK;
	}

	FingerprintPasswordGateWindow* window
		= new FingerprintPasswordGateWindow(salt, hash);
	return window->RequestAccess();
}


FingerprintPasswordGateWindow::FingerprintPasswordGateWindow(
	const uint8* salt, const uint8* hash)
	:
	BWindow(BRect(140, 140, 440, 240),
		B_TRANSLATE("Fingerprint settings"),
		B_TITLED_WINDOW_LOOK, B_MODAL_ALL_WINDOW_FEEL,
		B_NOT_ZOOMABLE | B_NOT_MINIMIZABLE | B_ASYNCHRONOUS_CONTROLS
			| B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE),
	fDoneSem(-1),
	fResult(B_CANCELED)
{
	memcpy(fSalt, salt, sizeof(fSalt));
	memcpy(fHash, hash, sizeof(fHash));

	fDoneSem = create_sem(0, "fingerprint password gate");

	fPassword = new BTextControl(B_TRANSLATE("Master password:"), "", NULL);
	fPassword->TextView()->HideTyping(true);

	fStatus = new BStringView("status", "");

	fUnlockButton = new BButton("unlock", B_TRANSLATE("Unlock"),
		new BMessage(kMsgUnlock));
	fCancelButton = new BButton("cancel", B_TRANSLATE("Cancel"),
		new BMessage(kMsgCancel));

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(new BStringView("explain",
			B_TRANSLATE("Enter the master password to manage fingerprints.")))
		.Add(fPassword)
		.Add(fStatus)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(fCancelButton)
			.Add(fUnlockButton)
		.End()
	.End();

	fUnlockButton->MakeDefault(true);
	fPassword->MakeFocus(true);
}


FingerprintPasswordGateWindow::~FingerprintPasswordGateWindow()
{
	if (fDoneSem >= 0)
		delete_sem(fDoneSem);
}


void
FingerprintPasswordGateWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgUnlock:
		{
			if (PasswordMatches(fPassword->Text(), fSalt, fHash)) {
				fResult = B_OK;
				release_sem(fDoneSem);
			} else {
				fStatus->SetText(B_TRANSLATE("Incorrect password."));
				fPassword->SetText("");
				fPassword->MakeFocus(true);
			}
			break;
		}

		case kMsgCancel:
			fResult = B_CANCELED;
			release_sem(fDoneSem);
			break;

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


bool
FingerprintPasswordGateWindow::QuitRequested()
{
	fResult = B_CANCELED;
	release_sem(fDoneSem);
	return false;
}


status_t
FingerprintPasswordGateWindow::RequestAccess()
{
	CenterOnScreen();
	Show();

	while (acquire_sem(fDoneSem) == B_INTERRUPTED)
		;

	status_t result = fResult;

	LockLooper();
	Quit();
	return result;
}
