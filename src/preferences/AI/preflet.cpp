/*
 * Copyright 2026, I Pirati Del Frico
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include <Application.h>
#include "PrefletWindow.h"

class PrefletApp : public BApplication {
public:
    PrefletApp() : BApplication("application/x-vnd.Haiku-AI") {}
    void ReadyToRun() override {
    	SetPulseRate(5000000);
        PrefletWindow* w = new PrefletWindow();
        w->Show();
        if (w->Lock()) {
            // Use a fixed initial size to avoid ResizeToPreferred race/crash
            w->ResizeTo(600, 520);
            w->Unlock();
        }
    }
    void Pulse() override {
    	be_app->WindowAt(0)->PostMessage(MSG_CHECK_SESSIONS);
    }
};

int main()
{
    PrefletApp app;
    app.Run();
    return 0;
}
