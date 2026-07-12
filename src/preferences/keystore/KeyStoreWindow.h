/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef KEYSTORE_WINDOW_H
#define KEYSTORE_WINDOW_H

#include <Window.h>
#include <ListView.h>
#include <StringItem.h>
#include <Key.h>

class BButton;
class BTextView;
class BScrollView;

class KeyringItem : public BStringItem {
public:
    KeyringItem(const char* name)
        : BStringItem(name), fName(name) {}
    const char* Name() const { return fName.String(); }
private:
    BString fName;
};

class KeyItem : public BStringItem {
public:
    KeyItem(const BKey& key);
    const BKey& Key() const { return fKey; }
private:
    BKey fKey;
};

class KeyStoreWindow : public BWindow {
public:
    KeyStoreWindow();
    virtual ~KeyStoreWindow();

    virtual void MessageReceived(BMessage* msg);
    virtual bool QuitRequested();

private:
    void _RefreshKeyrings();
    void _RefreshKeys();
    void _UpdateKeyDetails();
    void _LockSelectedKeyring();
    void _RemoveSelectedKey();

    BListView*   fKeyringsList;
    BScrollView* fKeyringsScroll;

    BListView*   fKeysList;
    BScrollView* fKeysScroll;

    BTextView*   fDetailsView;
    BScrollView* fDetailsScroll;

    BButton*     fChangeMasterButton;
    BButton*     fLockKeyringButton;
    BButton*     fRemoveKeyButton;
};

#endif // KEYSTORE_WINDOW_H
