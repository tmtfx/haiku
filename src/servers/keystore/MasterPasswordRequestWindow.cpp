/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT License.
 */


#include "MasterPasswordRequestWindow.h"

#include <Button.h>
#include <Catalog.h>
#include <GroupLayout.h>
#include <GroupView.h>
#include <SpaceLayoutItem.h>
#include <StringView.h>
#include <TextControl.h>
#include <TextView.h>
#include <View.h>

#include <cmath>
#include <new>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "MasterPasswordRequestWindow"


static const uint32 kMessageCancel  = 'mpcl';
static const uint32 kMessageConfirm = 'mpcf';


class MasterPasswordRequestView : public BView {
public:
	MasterPasswordRequestView()
		:
		BView("MasterPasswordRequestView", B_WILL_DRAW),
		fPassword(NULL),
		fConfirmButton(NULL),
		fCancelButton(NULL)
	{
		SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

		float inset = ceilf(be_plain_font->Size() * 0.7f);

		BGroupLayout* root = new(std::nothrow) BGroupLayout(B_VERTICAL);
		if (root == NULL)
			return;
		SetLayout(root);
		root->SetInsets(inset, inset, inset, inset);
		root->SetSpacing(inset);

		// Explanatory text
		BTextView* message = new(std::nothrow) BTextView("message");
		if (message == NULL)
			return;

		message->SetText(B_TRANSLATE(
			"An operation requires access to an encrypted key.\n"
			"Please enter the master password to unlock it.\n\n"
			"The password will be kept in memory until the system "
			"is shut down or the keystore server is restarted."));
		message->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
		rgb_color textColor = ui_color(B_PANEL_TEXT_COLOR);
		message->SetFontAndColor(be_plain_font, B_FONT_ALL, &textColor);
		message->MakeEditable(false);
		message->MakeSelectable(false);
		message->SetWordWrap(true);
		message->SetExplicitMinSize(BSize(
			message->StringWidth("M") * 42, B_SIZE_UNSET));
		root->AddView(message);

		// Password field
		fPassword = new(std::nothrow) BTextControl(
			B_TRANSLATE("Master Password:"), "", NULL);
		if (fPassword == NULL)
			return;
			
		fPassword->Mask(true);

		BLayoutItem* textItem = fPassword->CreateTextViewLayoutItem();
		textItem->SetExplicitMinSize(BSize(
			fPassword->StringWidth("0123456789012345678901234567890123456789")
				+ inset, B_SIZE_UNSET));

		BGroupView* fieldRow = new(std::nothrow) BGroupView(B_HORIZONTAL);
		if (fieldRow == NULL)
			return;
		fieldRow->GroupLayout()->SetSpacing(inset);
		fieldRow->GroupLayout()->AddItem(fPassword->CreateLabelLayoutItem());
		fieldRow->GroupLayout()->AddItem(textItem);
		root->AddView(fieldRow);

		// Buttons
		BGroupView* buttons = new(std::nothrow) BGroupView(B_HORIZONTAL);
		if (buttons == NULL)
			return;
		buttons->GroupLayout()->SetSpacing(inset);

		fCancelButton = new(std::nothrow) BButton(
			B_TRANSLATE("Cancel"), new BMessage(kMessageCancel));
		buttons->GroupLayout()->AddView(fCancelButton);
		buttons->GroupLayout()->AddItem(BSpaceLayoutItem::CreateGlue());

		fConfirmButton = new(std::nothrow) BButton(
			B_TRANSLATE("Unlock"), new BMessage(kMessageConfirm));
		buttons->GroupLayout()->AddView(fConfirmButton);
		root->AddView(buttons);
	}

	virtual void
	AttachedToWindow()
	{
		fCancelButton->SetTarget(Window());
		fConfirmButton->SetTarget(Window());
		fConfirmButton->MakeDefault(true);
		fPassword->MakeFocus();
	}

	const char*
	Password() const
	{
		return fPassword != NULL ? fPassword->Text() : "";
	}

private:
	BTextControl*	fPassword;
	BButton*		fConfirmButton;
	BButton*		fCancelButton;
};


// #pragma mark - MasterPasswordRequestWindow


MasterPasswordRequestWindow::MasterPasswordRequestWindow()
	:
	BWindow(BRect(50, 50, 100, 100),
		B_TRANSLATE_COMMENT("Master Password", "Window title"),
		B_TITLED_WINDOW,
		B_NOT_RESIZABLE | B_ASYNCHRONOUS_CONTROLS | B_NOT_ZOOMABLE
			| B_NOT_MINIMIZABLE | B_AUTO_UPDATE_SIZE_LIMITS
			| B_CLOSE_ON_ESCAPE),
	fRequestView(NULL),
	fDoneSem(-1),
	fResult(B_ERROR)
{
	fDoneSem = create_sem(0, "master password dialog");
	if (fDoneSem < 0)
		return;

	BGroupLayout* layout = new(std::nothrow) BGroupLayout(B_HORIZONTAL);
	if (layout == NULL)
		return;
	SetLayout(layout);

	fRequestView = new(std::nothrow) MasterPasswordRequestView();
	if (fRequestView == NULL)
		return;
	layout->AddView(fRequestView);
}


MasterPasswordRequestWindow::~MasterPasswordRequestWindow()
{
	if (fDoneSem >= 0)
		delete_sem(fDoneSem);
}


bool
MasterPasswordRequestWindow::QuitRequested()
{
	fResult = B_CANCELED;
	release_sem(fDoneSem);
	return false;
}


void
MasterPasswordRequestWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMessageCancel:
		case kMessageConfirm:
			fResult = (message->what == kMessageConfirm) ? B_OK : B_CANCELED;
			release_sem(fDoneSem);
			return;
	}
	BWindow::MessageReceived(message);
}


status_t
MasterPasswordRequestWindow::RequestPassword(BString& passwordOut)
{
	ResizeToPreferred();
	CenterOnScreen();
	Show();

	while (acquire_sem(fDoneSem) == B_INTERRUPTED)
		;

	status_t result = fResult;
	if (result == B_OK)
		passwordOut.SetTo(fRequestView->Password());

	LockLooper();
	Quit();
	return result;
}
