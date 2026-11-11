/*
 * Copyright 2003-2007, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Michael Phipps
 *		Jérôme Duval, jerome.duval@free.fr
 */
#ifndef PASSWORD_WINDOW_H
#define PASSWORD_WINDOW_H


#include <TextControl.h>
#include <Window.h>


const static int32 kMsgUnlock = 'ULMS';
const static int32 kMsgFingerprintStart = 'ULFs';


class BButton;


class PasswordWindow : public BWindow {
	public:
		PasswordWindow();

		const char *Password() { return fPassword->Text(); }
		void SetPassword(const char* text);
		void SetFingerprintAvailable(bool available);

	private:
		BTextControl *fPassword;
		BButton *fFingerprintButton;
};

#endif // PASSWORDWINDOW_H
