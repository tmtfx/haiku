/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT License.
 */
#ifndef _MASTER_PASSWORD_REQUEST_WINDOW_H
#define _MASTER_PASSWORD_REQUEST_WINDOW_H


#include <Message.h>
#include <String.h>
#include <Window.h>


class MasterPasswordRequestView;


class MasterPasswordRequestWindow : public BWindow {
public:
								MasterPasswordRequestWindow();
	virtual						~MasterPasswordRequestWindow();

	virtual	bool				QuitRequested();
	virtual	void				MessageReceived(BMessage* message);

		// Blocks until the user confirms or cancels.
		// On B_OK, |passwordOut| contains the entered password.
		status_t				RequestPassword(BString& passwordOut);

private:
		MasterPasswordRequestView*	fRequestView;
		sem_id						fDoneSem;
		status_t					fResult;
};


#endif // _MASTER_PASSWORD_REQUEST_WINDOW_H
