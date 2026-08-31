/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include <Application.h>
#include <String.h>
#include "MainWindow.h"

class HMCPApp : public BApplication {
public:
    HMCPApp()
        : BApplication("application/x-vnd.Haiku-hmcp"),
          fContext("")
    {}
    
    void ArgvReceived(int32 argc, char** argv) override {
        if (argc > 1) {
            fContext = argv[1];
        }
    }

    void ReadyToRun() override {
        MainWindow* w = new MainWindow(fContext.IsEmpty() ? nullptr : fContext.String());
        w->Show();
    }

private:
    BString fContext;
};

int main() {
    HMCPApp app;
    app.Run();
    return 0;
}
