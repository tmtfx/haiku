/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include "KeyStoreWindow.h"
#include "PasswordWindow.h"
#include "ResetMasterWindow.h"
#include <Button.h>
#include <TextView.h>
#include <ScrollView.h>
#include <LayoutBuilder.h>
#include <KeyStore.h>
#include <Alert.h>
#include <Application.h>
#include <Catalog.h>
#include <stdio.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "KeyStoreWindow"

static const uint32 MSG_KEYRING_SELECTED   = 'KSEL';
static const uint32 MSG_KEY_SELECTED       = 'YSEL';
static const uint32 MSG_CHANGE_MASTER_PASS = 'CHMP';
static const uint32 MSG_RESET_MASTER_PASS  = 'RSMP';
static const uint32 MSG_LOCK_KEYRING       = 'LCKR';
static const uint32 MSG_REMOVE_KEY         = 'RMKY';

KeyItem::KeyItem(const BKey& key)
    : BStringItem("")
{
    fKey = key;
    BString label;
    if (strlen(key.SecondaryIdentifier()) > 0) {
        label.SetToFormat("%s  [%s]", key.Identifier(), key.SecondaryIdentifier());
    } else {
        label.SetTo(key.Identifier());
    }
    SetText(label.String());
}


KeyStoreWindow::KeyStoreWindow()
    : BWindow(BRect(100, 100, 750, 550), B_TRANSLATE("KeyStore Manager"),
        B_TITLED_WINDOW, B_AUTO_UPDATE_SIZE_LIMITS),
      fKeyringsList(NULL),
      fKeysList(NULL),
      fDetailsView(NULL)
{
    // Listview Portachiavi (Sinistra)
    fKeyringsList = new BListView("keyrings_list", B_SINGLE_SELECTION_LIST);
    fKeyringsList->SetSelectionMessage(new BMessage(MSG_KEYRING_SELECTED));
    fKeyringsScroll = new BScrollView("keyrings_scroll", fKeyringsList, B_WILL_DRAW, false, true);

    // Listview Chiavi (Destra - Alto)
    fKeysList = new BListView("keys_list", B_SINGLE_SELECTION_LIST);
    fKeysList->SetSelectionMessage(new BMessage(MSG_KEY_SELECTED));
    fKeysScroll = new BScrollView("keys_scroll", fKeysList, B_WILL_DRAW, false, true);

    // Box di Dettaglio Chiave (Destra - Basso)
    fDetailsView = new BTextView("details_view");
    fDetailsView->MakeEditable(false);
    fDetailsScroll = new BScrollView("details_scroll", fDetailsView, B_WILL_DRAW, false, true);

    // Pulsanti
    fChangeMasterButton = new BButton("change_master", B_TRANSLATE("Change Master Password" B_UTF8_ELLIPSIS),
        new BMessage(MSG_CHANGE_MASTER_PASS));
    fResetMasterButton = new BButton("reset_master", B_TRANSLATE("Reset Master Password" B_UTF8_ELLIPSIS),
        new BMessage(MSG_RESET_MASTER_PASS));
    fLockKeyringButton = new BButton("lock_keyring", B_TRANSLATE("Lock Keyring"),
        new BMessage(MSG_LOCK_KEYRING));
    fRemoveKeyButton = new BButton("remove_key", B_TRANSLATE("Remove Key"),
        new BMessage(MSG_REMOVE_KEY));

    fLockKeyringButton->SetEnabled(false);
    fRemoveKeyButton->SetEnabled(false);

    // Costruzione del Layout con BLayoutBuilder
    BLayoutBuilder::Group<>(this, B_HORIZONTAL, 10)
        .SetInsets(10)
        .AddGroup(B_VERTICAL, 10, 1) // Lato Sinistro: Portachiavi
            .Add(fKeyringsScroll)
            .Add(fChangeMasterButton)
            .Add(fResetMasterButton)
        .End()
        .AddGroup(B_VERTICAL, 10, 2) // Lato Destro: Chiavi & Dettagli
            .Add(fKeysScroll, 3)
            .Add(fDetailsScroll, 2)
            .AddGroup(B_HORIZONTAL, 10)
                .AddGlue()
                .Add(fLockKeyringButton)
                .Add(fRemoveKeyButton)
            .End()
        .End()
    .End();

    _RefreshKeyrings();
}

KeyStoreWindow::~KeyStoreWindow()
{
}

void KeyStoreWindow::MessageReceived(BMessage* msg)
{
    switch (msg->what) {
        case MSG_KEYRING_SELECTED:
            _RefreshKeys();
            break;
        case MSG_KEY_SELECTED:
            _UpdateKeyDetails();
            break;
        case MSG_CHANGE_MASTER_PASS: {
            PasswordWindow* pw = new PasswordWindow(this);
            pw->Show();
            break;
        }
        case MSG_RESET_MASTER_PASS: {
            ResetMasterWindow* rm = new ResetMasterWindow(this);
            rm->Show();
            break;
        }
        case MSG_LOCK_KEYRING:
            _LockSelectedKeyring();
            break;
        case MSG_REMOVE_KEY:
            _RemoveSelectedKey();
            break;
        default:
            BWindow::MessageReceived(msg);
            break;
    }
}

bool KeyStoreWindow::QuitRequested()
{
    be_app->PostMessage(B_QUIT_REQUESTED);
    return true;
}

void KeyStoreWindow::_RefreshKeyrings()
{
    fKeyringsList->MakeEmpty();
    fKeysList->MakeEmpty();
    fDetailsView->SetText("");
    fLockKeyringButton->SetEnabled(false);
    fRemoveKeyButton->SetEnabled(false);

    BKeyStore store;
    uint32 cookie = 0;
    BString keyringName;

    // Aggiunge la Master Keyring (Predefinita di sistema)
    fKeyringsList->AddItem(new KeyringItem(B_TRANSLATE("Master")));

    // Scansiona le altre keyring nel Keystore
    while (store.GetNextKeyring(cookie, keyringName) == B_OK) {
        if (keyringName != "Master") {
            fKeyringsList->AddItem(new KeyringItem(keyringName.String()));
        }
    }
}

void KeyStoreWindow::_RefreshKeys()
{
    fKeysList->MakeEmpty();
    fDetailsView->SetText("");
    fRemoveKeyButton->SetEnabled(false);

    int32 selection = fKeyringsList->CurrentSelection();
    if (selection < 0) {
        fLockKeyringButton->SetEnabled(false);
        return;
    }

    KeyringItem* item = (KeyringItem*)fKeyringsList->ItemAt(selection);
    if (!item) return;

    BString keyringName = item->Name();
    fLockKeyringButton->SetEnabled(true);

    BKeyStore store;
    uint32 cookie = 0;
    BKey key;

    const char* keyringArg = (keyringName == B_TRANSLATE("Master")) ? NULL : keyringName.String();

    // Ottieni tutte le chiavi nel portachiavi selezionato
    while (store.GetNextKey(keyringArg, cookie, key) == B_OK) {
        fKeysList->AddItem(new KeyItem(key));
    }

    if (fKeysList->CountItems() == 0) {
        fKeysList->AddItem(new BStringItem(B_TRANSLATE("[No keys stored in this keyring]")));
    }
}

void KeyStoreWindow::_UpdateKeyDetails()
{
    fDetailsView->SetText("");
    fRemoveKeyButton->SetEnabled(false);

    int32 ringSel = fKeyringsList->CurrentSelection();
    int32 keySel = fKeysList->CurrentSelection();
    if (ringSel < 0 || keySel < 0) return;

    KeyringItem* ringItem = (KeyringItem*)fKeyringsList->ItemAt(ringSel);
    KeyItem* keyItem = dynamic_cast<KeyItem*>(fKeysList->ItemAt(keySel));
    if (!ringItem || !keyItem) return;

    fRemoveKeyButton->SetEnabled(true);

    const BKey& key = keyItem->Key();
    BString keyringName = ringItem->Name();
    const char* keyringArg = (keyringName == B_TRANSLATE("Master")) ? NULL : keyringName.String();

    BString details;
    details << B_TRANSLATE("Identifier: ") << key.Identifier() << "\n";
    if (strlen(key.SecondaryIdentifier()) > 0) {
        details << B_TRANSLATE("Secondary Identifier: ") << key.SecondaryIdentifier() << "\n";
    }

    BString typeStr = B_TRANSLATE("Generic");
    if (key.Type() == B_KEY_TYPE_PASSWORD) typeStr = B_TRANSLATE("Password");
    else if (key.Type() == B_KEY_TYPE_CERTIFICATE) typeStr = B_TRANSLATE("Certificate");
    details << B_TRANSLATE("Type: ") << typeStr << "\n";

    BKeyStore store;
    BKey fullKey;
    
    // Proviamo a recuperare la chiave sbloccata/decifrata dal Keystore
    status_t err = store.GetKey(keyringArg, key.Type(), key.Identifier(), key.SecondaryIdentifier(), false, fullKey);
    if (err == B_OK) {
        details << B_TRANSLATE("Status: Unlocked / Decrypted") << "\n";
        
        if (fullKey.Type() == B_KEY_TYPE_PASSWORD) {
            BPasswordKey* pwdKey = (BPasswordKey*)&fullKey;
            details << B_TRANSLATE("Value (Password): ") << pwdKey->Password() << "\n";
        } else {
            details << B_TRANSLATE("Value (Data length): ") << (int32)fullKey.DataLength() << B_TRANSLATE(" bytes") << "\n";
        }
    } else {
        details << B_TRANSLATE("Status: Locked / Encrypted") << "\n";
        details << B_TRANSLATE("Error loading key content: ") << strerror(err) << "\n";
    }

    fDetailsView->SetText(details.String());
}

void KeyStoreWindow::_LockSelectedKeyring()
{
    int32 selection = fKeyringsList->CurrentSelection();
    if (selection < 0) return;

    KeyringItem* item = (KeyringItem*)fKeyringsList->ItemAt(selection);
    if (!item) return;

    BString keyringName = item->Name();
    const char* keyringArg = (keyringName == B_TRANSLATE("Master")) ? NULL : keyringName.String();

    BKeyStore store;
    status_t err;
    if (keyringArg == NULL) {
        err = store.LockMasterKeyring();
    } else {
        err = store.LockKeyring(keyringArg);
    }

    if (err == B_OK) {
        BAlert* alert = new BAlert(B_TRANSLATE("Locked"),
            B_TRANSLATE("Keyring locked successfully."), B_TRANSLATE("OK"));
        alert->Go();
        _RefreshKeys();
    } else {
        BString errorMsg;
        errorMsg.SetToFormat(B_TRANSLATE("Failed to lock keyring: %s"), strerror(err));
        BAlert* alert = new BAlert(B_TRANSLATE("Error"), errorMsg.String(), B_TRANSLATE("OK"));
        alert->Go();
    }
}

void KeyStoreWindow::_RemoveSelectedKey()
{
    int32 ringSel = fKeyringsList->CurrentSelection();
    int32 keySel = fKeysList->CurrentSelection();
    if (ringSel < 0 || keySel < 0) return;

    KeyringItem* ringItem = (KeyringItem*)fKeyringsList->ItemAt(ringSel);
    KeyItem* keyItem = dynamic_cast<KeyItem*>(fKeysList->ItemAt(keySel));
    if (!ringItem || !keyItem) return;

    const BKey& key = keyItem->Key();
    BString keyringName = ringItem->Name();
    const char* keyringArg = (keyringName == B_TRANSLATE("Master")) ? NULL : keyringName.String();

    BAlert* alert = new BAlert(B_TRANSLATE("Confirm Delete"),
        B_TRANSLATE("Are you sure you want to permanently delete this key?"),
        B_TRANSLATE("Cancel"), B_TRANSLATE("Delete"), nullptr, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
    
    if (alert->Go() == 1) {
        BKeyStore store;
        status_t err = store.RemoveKey(keyringArg, key);
        if (err == B_OK) {
            _RefreshKeys();
        } else {
            BString errorMsg;
            errorMsg.SetToFormat(B_TRANSLATE("Failed to delete key: %s"), strerror(err));
            BAlert* errorAlert = new BAlert(B_TRANSLATE("Error"), errorMsg.String(), B_TRANSLATE("OK"));
            errorAlert->Go();
        }
    }
}
