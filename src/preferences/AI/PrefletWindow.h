/*
 * Copyright 2026, I Pirati Del Frico
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef PREFLET_WINDOW_H
#define PREFLET_WINDOW_H

#include <Window.h>
#include <ListView.h>
#include <ScrollView.h>
#include <CheckBox.h>
#include <TextView.h>
#include <StringView.h>
#include <String.h>
#include <Node.h>
#include <RadioButton.h>


class BMenuField;
class BPopUpMenu;
class BButton;
class BTextControl;

static const uint32 MSG_CHECK_SESSIONS = 'CHKS';

// ListItem personalizzato per mantenere lo stato dei plugin
class PluginItem : public BStringItem {
public:
    PluginItem(const char* name) : BStringItem(name) {}
};

class PrefletWindow : public BWindow {
public:
    PrefletWindow();
    virtual ~PrefletWindow();
    
    virtual bool QuitRequested();
    virtual void MessageReceived(BMessage* msg);

private:
    void _UpdateClearButton();
    void _UpdateApiKeyField();
    void _UpdatePluginDetails();
    void _UpdateContextDetails();
    void _RefreshContexts();
    void _RefreshSessions();

    // Top Bar
    BPopUpMenu* fEngineMenu;
    BMenuField* fEngineMenuField;

    // Sezione Superiore: Plugins & Config
    BListView* fPluginListView;
    BScrollView* fPluginScrollView;
    
    BPopUpMenu* fModelMenu;
    BMenuField* fModelMenuField;
    BButton* fRefreshModelsButton;
    
    BTextControl* fApiKeyControl;
    BTextControl* fBaseUrlControl;
    BButton* fToggleApiKeyButton;
    BButton* fClearApiKeyButton;
    BCheckBox* fRemoteContextCheckBox;
    BCheckBox* fSystemInfoCheckBox;
    BCheckBox* fFileSystemCheckBox;

    BRadioButton*   fReadAlwaysRadio;
    BRadioButton* fReadAskRadio;
    BRadioButton* fReadNeverRadio;
    BRadioButton*   fWriteAlwaysRadio;
    BRadioButton* fWriteAskRadio;
    BRadioButton* fWriteNeverRadio;

    BCheckBox* fRunCommandsCheckBox;

    BCheckBox*   fBaseUrlOverrideCheckBox;

    // Sezione Centrale: Contesti
    BListView* fContextListView;
    BScrollView* fContextScrollView;
    BStringView* fContextIdView;
    BTextView* fContextTextView;
    BScrollView* fContextTextScrollView;

    // Sezione Inferiore: Sessioni Attive
    BListView* fSessionListView;
    BScrollView* fSessionScrollView;

    // Bottom Bar Buttons
    BButton* fSaveButton;
    BButton* fApplyButton;

    // Stato Interno
    node_ref       fContextDirRef;
};

#endif // PREFLET_WINDOW_H
