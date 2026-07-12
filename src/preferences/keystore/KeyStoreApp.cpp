/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include <Application.h>
#include "KeyStoreWindow.h"

class KeyStoreApp : public BApplication {
public:
    KeyStoreApp()
        : BApplication("application/x-vnd.Haiku-KeyStore")
    {
    }

    virtual void ReadyToRun() override
    {
        KeyStoreWindow* window = new KeyStoreWindow();
        window->Show();
    }
};

int main()
{
    KeyStoreApp app;
    app.Run();
    return 0;
}
