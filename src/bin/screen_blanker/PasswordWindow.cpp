/*
 * Copyright 2003-2009, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Michael Phipps
 *		Jérôme Duval, jerome.duval@free.fr
 *		Julun <host.haiku@gmx.de>
 */


#include "PasswordWindow.h"

#include <Application.h>
#include <Box.h>
#include <Button.h>
#include <Catalog.h>
#include <Screen.h>

#include <WindowPrivate.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Screensaver password dialog"


PasswordWindow::PasswordWindow()
	:
	BWindow(BRect(100, 100, 400, 230), "Enter password",
		B_NO_BORDER_WINDOW_LOOK, kPasswordWindowFeel
			/* TODO: B_MODAL_APP_WINDOW_FEEL should also behave correctly */,
		B_NOT_MOVABLE | B_NOT_CLOSABLE | B_NOT_ZOOMABLE | B_NOT_MINIMIZABLE
		| B_NOT_RESIZABLE | B_ASYNCHRONOUS_CONTROLS, B_ALL_WORKSPACES)
{
	BView* topView = new BView(Bounds(), "topView", B_FOLLOW_ALL, B_WILL_DRAW);
	topView->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	topView->SetHighUIColor(B_PANEL_TEXT_COLOR);
	AddChild(topView);

	BRect bounds(Bounds());
	bounds.InsetBy(10.0, 10.0);

	BBox *customBox = new BBox(bounds, "customBox", B_FOLLOW_NONE);
	topView->AddChild(customBox);
	customBox->SetLabel(B_TRANSLATE("Unlock screen saver"));

	bounds.top += 10.0;
	fPassword = new BTextControl(bounds, "password",
		B_TRANSLATE("Enter password:"), "VeryLongPasswordPossible",
		NULL, B_FOLLOW_NONE);
	customBox->AddChild(fPassword);
	fPassword->MakeFocus(true);
	fPassword->ResizeToPreferred();
	fPassword->TextView()->HideTyping(true);
	fPassword->SetDivider(be_plain_font->StringWidth(
		B_TRANSLATE_NOCOLLECT("Enter password:")) + 5.0);

	BButton* button = new BButton(BRect(), "unlock", B_TRANSLATE("Unlock"),
		new BMessage(kMsgUnlock), B_FOLLOW_NONE);
	customBox->AddChild(button);
	button->MakeDefault(true);
	button->ResizeToPreferred();
	button->SetTarget(NULL, be_app);

	// Hidden/disabled by default - ScreenBlanker enables this only if
	// fingerprint unlock is turned on in the Fingerprint preflet and at
	// least one finger is enrolled. Clicking it does not try to reconstruct
	// the screensaver's own crypt()-hashed password at all: unlike the
	// master-password/keystore integration, there is nothing to decrypt
	// here, so a live match alone is enough to authorize the unlock.
	fFingerprintButton = new BButton(BRect(), "fingerprint",
		B_TRANSLATE("Usa impronta"), new BMessage(kMsgFingerprintStart),
		B_FOLLOW_NONE);
	customBox->AddChild(fFingerprintButton);
	fFingerprintButton->ResizeToPreferred();
	fFingerprintButton->SetTarget(NULL, be_app);
	fFingerprintButton->SetEnabled(false);

	BRect frame = fPassword->Frame();
	button->MoveTo(frame.right - button->Bounds().Width(), frame.bottom + 10.0);
	fFingerprintButton->MoveTo(frame.left, button->Frame().bottom + 10.0);

	float boxWidth = frame.right;
	if (fFingerprintButton->Frame().right > boxWidth)
		boxWidth = fFingerprintButton->Frame().right;
	customBox->ResizeTo(boxWidth + 10.0, fFingerprintButton->Frame().bottom + 10.0);

	frame = customBox->Frame();
	ResizeTo(frame.right + 10.0, frame.bottom + 10.0);

	BScreen screen(this);
	MoveTo(screen.Frame().left + (screen.Frame().Width() - Bounds().Width()) / 2,
		screen.Frame().top + (screen.Frame().Height() - Bounds().Height()) / 2);
}


void
PasswordWindow::SetPassword(const char* text)
{
	if (Lock()) {
		fPassword->SetText(text);
		fPassword->MakeFocus(true);
		Unlock();
	}
}


void
PasswordWindow::SetFingerprintAvailable(bool available)
{
	if (Lock()) {
		fFingerprintButton->SetEnabled(available);
		Unlock();
	}
}
