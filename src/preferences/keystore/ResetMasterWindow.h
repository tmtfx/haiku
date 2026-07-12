/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef RESET_MASTER_WINDOW_H
#define RESET_MASTER_WINDOW_H

#include <Window.h>

class BTextControl;
class BButton;

class ResetMasterWindow : public BWindow {
public:
    ResetMasterWindow(BWindow* parent);
    virtual ~ResetMasterWindow();

    virtual void MessageReceived(BMessage* msg);

private:
    void _OnReset();
    status_t _WriteMasterPasswordShadow(uint8* outSalt);
    status_t _WriteKeystore(const uint8* salt);

    BTextControl* fPasswordControl;
    BTextControl* fConfirmControl;
    BButton*      fResetButton;
    BButton*      fCancelButton;
    BWindow*      fParent;
};

#endif // RESET_MASTER_WINDOW_H
