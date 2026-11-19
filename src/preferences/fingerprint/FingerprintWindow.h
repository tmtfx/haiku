/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef FINGERPRINT_WINDOW_H
#define FINGERPRINT_WINDOW_H


#include <Window.h>


class BButton;
class BCheckBox;
class BListView;
class BStringView;


class FingerprintWindow : public BWindow {
public:
								FingerprintWindow();
	virtual						~FingerprintWindow();

	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				QuitRequested();

private:
			void				_RefreshList();
			void				_StartEnroll();
			void				_StartDelete();
			void				_StartVerify();
			void				_ToggleMasterPasswordUnlock(bool enable);
			void				_ToggleScreensaverUnlock(bool enable);
			void				_SetBusy(bool busy, const char* status);

	static	status_t			_EnrollThreadEntry(void* data);
	static	status_t			_VerifyThreadEntry(void* data);

			BListView*			fListView;
			BButton*			fEnrollButton;
			BButton*			fDeleteButton;
			BButton*			fVerifyButton;
			BCheckBox*			fMasterPasswordCheckBox;
			BCheckBox*			fScreensaverCheckBox;
			BStringView*		fStatusView;
};


#endif // FINGERPRINT_WINDOW_H
