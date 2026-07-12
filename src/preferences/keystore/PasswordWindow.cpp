/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include "PasswordWindow.h"
#include <TextControl.h>
#include <Button.h>
#include <LayoutBuilder.h>
#include <KeyStore.h>
#include <Alert.h>
#include <Catalog.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "PasswordWindow"

static const uint32 MSG_SAVE_PASSWORD = 'SAVP';
static const uint32 MSG_CANCEL_PASSWORD = 'CANP';

PasswordWindow::PasswordWindow(BWindow* parent)
    : BWindow(BRect(150, 150, 450, 300), B_TRANSLATE("Change Master Password"),
        B_TITLED_WINDOW, B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS),
      fParent(parent)
{
    fPasswordControl = new BTextControl("new_pass", B_TRANSLATE("New Master Password:"), "", nullptr);
    fPasswordControl->TextView()->HideTyping(true);

    fConfirmControl = new BTextControl("confirm_pass", B_TRANSLATE("Confirm Password:"), "", nullptr);
    fConfirmControl->TextView()->HideTyping(true);

    fSaveButton = new BButton("save", B_TRANSLATE("Save"), new BMessage(MSG_SAVE_PASSWORD));
    fCancelButton = new BButton("cancel", B_TRANSLATE("Cancel"), new BMessage(MSG_CANCEL_PASSWORD));

    BLayoutBuilder::Group<>(this, B_VERTICAL, 10)
        .SetInsets(15)
        .Add(fPasswordControl)
        .Add(fConfirmControl)
        .AddGroup(B_HORIZONTAL, 10)
            .AddGlue()
            .Add(fCancelButton)
            .Add(fSaveButton)
        .End()
    .End();

    fPasswordControl->MakeFocus(true);
}

PasswordWindow::~PasswordWindow()
{
}

void PasswordWindow::MessageReceived(BMessage* msg)
{
    switch (msg->what) {
        case MSG_SAVE_PASSWORD:
            _OnSave();
            break;
        case MSG_CANCEL_PASSWORD:
            PostMessage(B_QUIT_REQUESTED);
            break;
        default:
            BWindow::MessageReceived(msg);
            break;
    }
}

void PasswordWindow::_OnSave()
{
    BString password = fPasswordControl->Text();
    BString confirm = fConfirmControl->Text();

    if (password.IsEmpty()) {
        BAlert* alert = new BAlert(B_TRANSLATE("Error"),
            B_TRANSLATE("Password cannot be empty!"), B_TRANSLATE("OK"));
        alert->Go();
        return;
    }

    if (password != confirm) {
        BAlert* alert = new BAlert(B_TRANSLATE("Error"),
            B_TRANSLATE("Passwords do not match!"), B_TRANSLATE("OK"));
        alert->Go();
        return;
    }

    BKeyStore store;
    BPasswordKey key(password.String(), B_KEY_PURPOSE_KEYRING, "");
    status_t err = store.SetMasterUnlockKey(key);

    if (err == B_OK) {
        BAlert* alert = new BAlert(B_TRANSLATE("Success"),
            B_TRANSLATE("Master password changed successfully!"), B_TRANSLATE("OK"));
        alert->Go();
        PostMessage(B_QUIT_REQUESTED);
    } else {
        BString errorMsg;
        errorMsg.SetToFormat(B_TRANSLATE("Failed to change master password: %s"), strerror(err));
        BAlert* alert = new BAlert(B_TRANSLATE("Error"), errorMsg.String(), B_TRANSLATE("OK"));
        alert->Go();
    }
}
