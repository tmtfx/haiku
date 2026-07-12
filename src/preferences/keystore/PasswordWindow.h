/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef PASSWORD_WINDOW_H
#define PASSWORD_WINDOW_H

#include <Window.h>

class BTextControl;
class BButton;

class PasswordWindow : public BWindow {
public:
    PasswordWindow(BWindow* parent);
    virtual ~PasswordWindow();

    virtual void MessageReceived(BMessage* msg);

private:
    void _OnSave();

    BTextControl* fPasswordControl;
    BTextControl* fConfirmControl;
    BButton*      fSaveButton;
    BButton*      fCancelButton;
    BWindow*      fParent;
};

#endif // PASSWORD_WINDOW_H
