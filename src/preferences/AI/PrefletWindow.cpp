/*
 * Copyright 2026, I Pirati Del Frico
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include "PrefletWindow.h"
#include <View.h>
#include <LayoutBuilder.h>
#include <TextControl.h>
#include <Button.h>
#include <Message.h>
#include <Alert.h>
#include <Messenger.h>
#include <MenuField.h>
#include <PopUpMenu.h>
#include <MenuItem.h>
#include <Application.h>
#include <Box.h>
#include <ScrollBar.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <NodeMonitor.h>
#include <Path.h>

#include <NodeInfo.h>
#include <Roster.h>

#include <Catalog.h>

#include <cstdio>
#include "AIConfig.h"
#include "AICommands.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "AI"

// Costanti dei messaggi rimaste invariate o aggiunte per la nuova UI
static const uint32 MSG_SAVE            = 'SAVE';
static const uint32 MSG_APPLY           = 'RLD!';
static const uint32 MSG_TOGGLE_KEY      = 'TGLK';
static const uint32 MSG_REFRESH_PLUGINS = 'RFMD';
static const uint32 MSG_MODEL_SELECTED  = 'MSEL';
static const uint32 MSG_PLUGINS_FETCHED = 'MFTC';
static const uint32 MSG_MODELS_FETCHED  = 'MDFH';
static const uint32 MSG_ENGINE_LOCAL    = 'ENGL';
static const uint32 MSG_ENGINE_REMOTE   = 'ENGR';
static const uint32 MSG_ENGINE_ALL      = 'ENGA';
static const uint32 MSG_PLUGIN_SELECTED = 'PSEL';
static const uint32 MSG_CLEAR_KEY       = 'CLRK';
static const uint32 MSG_CONTEXT_SELECTED= 'CTX_';
static const uint32 MSG_CONTEXT_OPEN    = 'CTXO';
static const uint32 MSG_TOGGLE_REM_CTX   = 'TGRC';
static const uint32 MSG_BASE_URL_OVERRIDE = 'BOVR';
static const uint32 MSG_SESSIONS_FETCHED = 'SFCH';

// Simple JSON array parser for flat arrays of strings: ["a","b"]
static void parse_models_json(const char* json, BPopUpMenu* menu) {
    if (!json || !menu) return;
    const char* p = json;
    while ((p = strchr(p, '"')) != nullptr) {
        p++; // skip quote
        const char* q = strchr(p, '"');
        if (!q) break;
        size_t len = q - p;
        BString name;
        name.SetTo(p, len);

        BMessage* msg = new BMessage(MSG_MODEL_SELECTED);
        msg->AddString("model", name.String());
        BMenuItem* item = new BMenuItem(name.String(), msg);
        menu->AddItem(item);
        p = q + 1;
    }
}

static status_t
FetchPluginsThread(void* data)
{
    BMessage* args = static_cast<BMessage*>(data);
    if (!args) return B_BAD_VALUE;

    BMessenger windowMessenger;
    BString engineType;
    args->FindMessenger("reply_to", &windowMessenger);
    args->FindString("engine_type", &engineType);

    BMessage pluginsResult;
    if (AIGetAvailablePlugins(engineType.String(), pluginsResult) == B_OK) {
        BMessage successMsg(MSG_PLUGINS_FETCHED);
        successMsg.AddMessage("plugins", &pluginsResult);
        windowMessenger.SendMessage(&successMsg);
    }

    delete args;
    return B_OK;
}

// Thread 2: Chiede ad ai_server i modelli di UN SINGOLO PLUGIN ('GMOD')
static status_t
FetchModelsThread(void* data)
{
    BMessage* args = static_cast<BMessage*>(data);
    if (!args) return B_BAD_VALUE;

    BMessenger windowMessenger;
    BString pluginName;
    args->FindMessenger("reply_to", &windowMessenger);
    args->FindString("plugin_name", &pluginName);

    // CHIAMATA AL KIT!
    BString jsonModels;
    if (AIGetPluginModels(pluginName.String(), jsonModels) == B_OK) {
        BMessage successMsg(MSG_MODELS_FETCHED);
        successMsg.AddString("plugin_models", jsonModels.String());
        windowMessenger.SendMessage(&successMsg);
    }

    delete args;
    return B_OK;
}

// Thread 3: Chiede ad ai_server la lista di tutte le sessioni attive ('GALS')
static status_t
FetchSessionsThread(void* data)
{
    BMessenger* messenger = static_cast<BMessenger*>(data);
    if (!messenger) return B_BAD_VALUE;

    BList sessionsList;
    status_t err = AIEngine::GetAllSessions(sessionsList);

    BMessage result(MSG_SESSIONS_FETCHED);
    if (err == B_OK) {
        result.AddInt32("count", sessionsList.CountItems());
        for (int32 i = 0; i < sessionsList.CountItems(); i++) {
            AISessionInfo* info = static_cast<AISessionInfo*>(sessionsList.ItemAt(i));
            if (info) {
                BMessage sessionMsg;
                sessionMsg.AddInt32("session_id", info->session_id);
                sessionMsg.AddString("context_id", info->context_id.String());
                sessionMsg.AddString("title", info->title.String());
                sessionMsg.AddString("plugin_name", info->plugin_name.String());
                sessionMsg.AddString("model_name", info->model_name.String());
                result.AddMessage("session", &sessionMsg);
                delete info;
            }
        }
    }
    // Inviamo anche se la lista è vuota, così la UI si svuota correttamente
    messenger->SendMessage(&result);

    delete messenger;
    return B_OK;
}

class PluginListItem : public BStringItem {
public:
    PluginListItem(const char* text, const char* type = "local", bool isDefault = false)
        : BStringItem(text),
          fType(type),
          fIsDefault(isDefault)
    {
    }

    virtual void DrawItem(BView* owner, BRect frame, bool complete = false) override
    {
        // Protezione se il testo è vuoto
        if (Text() == nullptr) return;

        // Comportamento standard per lo sfondo (selezione o focus)
        owner->PushState();
        
        if (IsSelected()) {
            owner->SetLowColor(ui_color(B_LIST_SELECTED_BACKGROUND_COLOR));
            owner->SetHighColor(ui_color(B_LIST_SELECTED_ITEM_TEXT_COLOR));
            owner->FillRect(frame, B_SOLID_LOW);
        } else {
            owner->SetLowColor(owner->ViewColor());
            // Se è il default, usiamo il BLU, altrimenti il testo standard del pannello
            if (fIsDefault) {
                owner->SetHighColor(0, 0, 220, 255); // Blu nativo
            } else {
                owner->SetHighColor(ui_color(B_LIST_ITEM_TEXT_COLOR));
            }
            owner->FillRect(frame, B_SOLID_LOW);
        }

        // Disegniamo il testo (spostato leggermente a destra per i margini standard)
        font_height fh;
        owner->GetFontHeight(&fh);
        float fontHeight = fh.ascent + fh.descent + fh.leading;
        float center = frame.top + (frame.Height() - fontHeight) / 2 + fh.ascent;

        owner->MovePenTo(frame.left + 4, center);
        owner->DrawString(Text());

        owner->PopState();
    }

    void SetDefault(bool isDefault) { fIsDefault = isDefault; }
    bool IsDefault() const { return fIsDefault; }
    const char* Type() const { return fType.String(); }

private:
    BString fType;
    bool fIsDefault;
};

class ContextListItem : public BStringItem {
public:
    ContextListItem(const char* label, const char* contextId, const char* filePath)
        : BStringItem(label), fContextId(contextId), fFilePath(filePath) {}

    const char* ContextId() const { return fContextId.String(); }
    const char* FilePath() const  { return fFilePath.String(); }

private:
    BString fContextId;
    BString fFilePath;
};

PrefletWindow::PrefletWindow()
    : BWindow(BRect(100, 100, 750, 800), B_TRANSLATE("AI Preflet"), B_TITLED_WINDOW, B_AUTO_UPDATE_SIZE_LIMITS | B_QUIT_ON_WINDOW_CLOSE)
{
    // 1. Menu Selezione Tipo Engine (In alto)
    fEngineMenu = new BPopUpMenu("engines");
    BMenuItem* localItem = new BMenuItem("local", new BMessage(MSG_ENGINE_LOCAL));
    BMenuItem* remoteItem = new BMenuItem("remote", new BMessage(MSG_ENGINE_REMOTE));
    BMenuItem* allItem = new BMenuItem("all", new BMessage(MSG_ENGINE_ALL));
    fEngineMenu->AddItem(localItem);
    fEngineMenu->AddItem(remoteItem);
    fEngineMenu->AddItem(allItem);
    fEngineMenuField = new BMenuField("engine", B_TRANSLATE("Engine Type:"), fEngineMenu);
    fEngineMenu->SetTargetForItems(this);

    // 2. Sezione Superiore: Plugins (Sinistra) & Dettagli (Destra)
    fPluginListView = new BListView("plugins_list", B_SINGLE_SELECTION_LIST);
    fPluginListView->SetSelectionMessage(new BMessage(MSG_PLUGIN_SELECTED));
    fPluginScrollView = new BScrollView("plugins_scroll", fPluginListView, B_WILL_DRAW | B_FRAME_EVENTS, false, true);

    fModelMenu = new BPopUpMenu("models");
    fModelMenuField = new BMenuField("model", B_TRANSLATE("Model:"), fModelMenu);
    fRefreshModelsButton = new BButton("rmodels", B_TRANSLATE("Refresh"), new BMessage(MSG_REFRESH_PLUGINS));
    
    fApiKeyControl = new BTextControl("api", B_TRANSLATE("API Key:"), "", nullptr);
    fApiKeyControl->Mask(true);
    fToggleApiKeyButton = new BButton("toggle", B_TRANSLATE("Show"), new BMessage(MSG_TOGGLE_KEY));
    fClearApiKeyButton = new BButton("clear", B_TRANSLATE("Clear"), new BMessage(MSG_CLEAR_KEY));
    fRemoteContextCheckBox = new BCheckBox("remote_ctx", B_TRANSLATE("Use Remote Context"), new BMessage(MSG_TOGGLE_REM_CTX));

    // Nuovo campo: Base URL (per plugin locali/custom)
    fBaseUrlControl = new BTextControl("base_url", B_TRANSLATE("Base URL:"), "", nullptr);

    // Override checkbox to allow editing base_url also for remote plugins
    fBaseUrlOverrideCheckBox = new BCheckBox("base_url_override", B_TRANSLATE("Override remote Base URL (Advanced)"), new BMessage(MSG_BASE_URL_OVERRIDE));

    fSystemInfoCheckBox = new BCheckBox("perm_sys_info", B_TRANSLATE("Allow System Info Tool"), nullptr);
    fFileSystemCheckBox = new BCheckBox("perm_file_sys", B_TRANSLATE("Allow File System Access Tool"), nullptr);
    fRunCommandsCheckBox = new BCheckBox("perm_run_cmds", B_TRANSLATE("Allow Run Terminal Commands Tool"), nullptr);

    BBox* pluginConfigBox = new BBox("plugin_config");
    pluginConfigBox->SetLabel(B_TRANSLATE("Plugin Configuration"));

    BBox* mcpPermissionsBox = new BBox("mcp_perms");
    mcpPermissionsBox->SetLabel(B_TRANSLATE("MCP Tool Permissions"));
    BLayoutBuilder::Group<>(mcpPermissionsBox, B_VERTICAL, 5)
        .SetInsets(10, 20, 10, 10)
        .Add(fSystemInfoCheckBox)
        .Add(fFileSystemCheckBox)
        .Add(fRunCommandsCheckBox)
    .End();

    // Layout della destra della sezione plugin
    BLayoutBuilder::Group<>(pluginConfigBox, B_VERTICAL, 10)
        .SetInsets(10, 20, 10, 10)
        .AddGroup(B_HORIZONTAL)
            .Add(fModelMenuField)
            .Add(fRefreshModelsButton)
        .End()
        .AddGroup(B_HORIZONTAL)
            .Add(fApiKeyControl)
            .Add(fToggleApiKeyButton)
            .Add(fClearApiKeyButton)
        .End()
        .AddGroup(B_HORIZONTAL)
            .Add(fBaseUrlControl)
            .Add(fBaseUrlOverrideCheckBox)
        .End()
        .Add(fRemoteContextCheckBox)
        .Add(mcpPermissionsBox)
        .AddGlue()
    .End();

    // 3. Sezione Centrale: Contesti (Box separato)
    BBox* contextBox = new BBox("context_box");
    contextBox->SetLabel(B_TRANSLATE("Saved Contexts"));

    fContextListView = new BListView("context_list", B_SINGLE_SELECTION_LIST);
    fContextListView->SetSelectionMessage(new BMessage(MSG_CONTEXT_SELECTED));
    fContextListView->SetInvocationMessage(new BMessage(MSG_CONTEXT_OPEN));
    fContextScrollView = new BScrollView("context_scroll", fContextListView, B_WILL_DRAW, false, true);

    fContextIdView = new BStringView("context_id", B_TRANSLATE("Context ID: None"));
    fContextIdView->SetFont(be_bold_font);

    fContextTextView = new BTextView("context_text");
    fContextTextView->MakeEditable(false);
    fContextTextScrollView = new BScrollView("context_text_scroll", fContextTextView, B_WILL_DRAW, false, true);

    BLayoutBuilder::Group<>(contextBox, B_HORIZONTAL, 10)
        .SetInsets(10, 20, 10, 10)
        .Add(fContextScrollView, 1)
        .AddGroup(B_VERTICAL, 5, 2)
            .Add(fContextIdView)
            .Add(fContextTextScrollView)
        .End()
    .End();

    // 4. Sezione Inferiore: Sessioni Attive (Box separato)
    BBox* sessionBox = new BBox("session_box");
    sessionBox->SetLabel(B_TRANSLATE("Active Sessions (Auto-refresh 5s)"));
    
    fSessionListView = new BListView("session_list", B_SINGLE_SELECTION_LIST);
    fSessionScrollView = new BScrollView("session_scroll", fSessionListView, B_WILL_DRAW, false, true);
    
    BLayoutBuilder::Group<>(sessionBox, B_VERTICAL)
        .SetInsets(10, 20, 10, 10)
        .Add(fSessionScrollView)
    .End();

    // Pulsanti di Azione Finali
    fSaveButton = new BButton("save", B_TRANSLATE("Save Settings"), new BMessage(MSG_SAVE));
    fApplyButton = new BButton("apply", B_TRANSLATE("Apply to ai_server"), new BMessage(MSG_APPLY));

    // COSTRUZIONE LAYOUT GENERALE DELLA PREFLET
    BView* root = new BView(Bounds(), "root", B_FOLLOW_ALL_SIDES, B_WILL_DRAW);
    root->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
    AddChild(root);

    BLayoutBuilder::Group<>(root, B_VERTICAL, 10)
        .SetInsets(10)
        .Add(fEngineMenuField) // In alto
        .AddGroup(B_HORIZONTAL, 10, 3) // Sezione Plugin (H: List | Config)
            .Add(fPluginScrollView, 1)
            .Add(pluginConfigBox, 2)
        .End()
        .Add(contextBox, 3)  // Sezione Contesti (Centrale)
        .Add(sessionBox, 1)  // Sezione Sessioni (Inferiore)
        .AddGroup(B_HORIZONTAL) // Bottoni di fondo
            .AddGlue()
            .Add(fSaveButton)
            .Add(fApplyButton)
        .End()
    .End();

    // Inizializzazioni e Caricamento Stati Precedenti
    // Su sistema vergine (nessun file settings), mostriamo tutti i plugin
    if (allItem) allItem->SetMarked(true);
    AISettings s;
    if (LoadAISettings(s)) {
        if (s.engine == "remote") {
            if (allItem) allItem->SetMarked(false);
            if (remoteItem) remoteItem->SetMarked(true);
        } else if (s.engine == "local") {
            if (allItem) allItem->SetMarked(false);
            if (localItem) localItem->SetMarked(true);
        } else if (s.engine == "all") {
            // already marked
        }
    }

    _UpdateClearButton();
    PostMessage(new BMessage(MSG_REFRESH_PLUGINS));

    // Avvia NodeMonitor sulla cartella dei contesti per aggiornamento istantaneo
    BPath ctxDirPath;
    if (find_directory(B_USER_SETTINGS_DIRECTORY, &ctxDirPath) == B_OK) {
        ctxDirPath.Append("ai_server/contexts");
        create_directory(ctxDirPath.Path(), 0755);
        BEntry dirEntry(ctxDirPath.Path());
        if (dirEntry.GetNodeRef(&fContextDirRef) == B_OK)
            watch_node(&fContextDirRef, B_WATCH_DIRECTORY, this);
    }
    _RefreshContexts();
}

PrefletWindow::~PrefletWindow()
{
    stop_watching(this);
}

bool PrefletWindow::QuitRequested() {
    be_app->PostMessage(B_QUIT_REQUESTED);
    return true;
}

void PrefletWindow::MessageReceived(BMessage* msg)
{
    switch (msg->what) {
        case MSG_ENGINE_LOCAL:
        case MSG_ENGINE_REMOTE:
        case MSG_ENGINE_ALL: {
            // Gestione cambio filtri come prima
            fPluginListView->MakeEmpty();
            fModelMenu->RemoveItems(0, fModelMenu->CountItems(), true);
            PostMessage(new BMessage(MSG_REFRESH_PLUGINS));
            break;
        }
        case MSG_REFRESH_PLUGINS: {
            fprintf(stderr, "\n[LOG] --- INIZIO MSG_REFRESH_PLUGINS ---\n");
            
            // 1. Pulizia della UI: svuotiamo la lista dei plugin e il menu dei modelli
            fPluginListView->MakeEmpty();
            fModelMenu->RemoveItems(0, fModelMenu->CountItems(), true);
            fModelMenuField->SetEnabled(false);

            // 2. Preparazione degli argomenti per il thread
            BMessage* threadArgs = new BMessage();
            threadArgs->AddMessenger("reply_to", BMessenger(this));

            // Recuperiamo l'engine attualmente marcato nella tendina in alto
            BString currentEngine = "local";
            BMenuItem* markedEngine = fEngineMenu->FindMarked();
            if (markedEngine) {
                currentEngine = markedEngine->Label();
            }
            threadArgs->AddString("engine_type", currentEngine.String());

            // 3. Lancio del thread asincrono
            fprintf(stderr, "[LOG] Lancio il thread FetchPluginsThread per engine: '%s'\n", currentEngine.String());
            thread_id fetchThread = spawn_thread(FetchPluginsThread, "AI Plugins Fetcher", B_NORMAL_PRIORITY, threadArgs);
            
            if (fetchThread >= B_OK) {
                resume_thread(fetchThread);
            } else {
                fprintf(stderr, "[LOG] ERRORE: Impossibile spawnare FetchPluginsThread\n");
                delete threadArgs; // Evitiamo memory leak se il thread fallisce lo spawn
            }
            
            fprintf(stderr, "[LOG] --- FINE MSG_REFRESH_PLUGINS ---\n");
            break;
        }
        case MSG_PLUGINS_FETCHED: {
            fprintf(stderr, "\n[LOG] --- INIZIO MSG_PLUGINS_FETCHED ---\n");
            BMessage plugins;
            if (msg->FindMessage("plugins", &plugins) == B_OK) {
                
                // Carichiamo le impostazioni salvate per sapere qual è il plugin di default
                AISettings s;
                LoadAISettings(s);
                
                    int32 i = 0;
                BMessage pluginMsg;
                int32 defaultIndex = -1;

                // Ora il server fornisce una lista di messaggi plugin { "plugin_name", "plugin_type" }
                while (plugins.FindMessage("plugin", i, &pluginMsg) == B_OK) {
                    const char* pName = nullptr;
                    const char* pType = "local";
                    pluginMsg.FindString("plugin_name", &pName);
                    pluginMsg.FindString("plugin_type", &pType);

                    if (!pName) { i++; continue; }

                    bool isDefault = (s.plugin.Length() > 0 && s.plugin == pName);

                    PluginListItem* newItem = new PluginListItem(pName, pType, isDefault);
                    fPluginListView->AddItem(newItem);

                    if (isDefault) {
                        defaultIndex = i;
                    }
                    i++;
                }

                // Gestione della selezione automatica al boot/refresh
                if (fPluginListView->CountItems() > 0) {
                    if (defaultIndex != -1) {
                        fprintf(stderr, "[LOG] Seleziono automaticamente il plugin di default all'indice %" B_PRId32 "\n", defaultIndex);
                        fPluginListView->Select(defaultIndex);
                    } else {
                        fprintf(stderr, "[LOG] Nessun match con il plugin salvato. Seleziono il primo della lista.\n");
                        fPluginListView->Select(0);
                    }
                }
            }
            fprintf(stderr, "[LOG] --- FINE MSG_PLUGINS_FETCHED ---\n");
            break;
        }
        case MSG_PLUGIN_SELECTED: {
            _UpdatePluginDetails();
            break;
        }
        case MSG_MODELS_FETCHED: {
            fprintf(stderr, "\n[LOG] --- INIZIO MSG_MODELS_FETCHED ---\n");
            fModelMenu->RemoveItems(0, fModelMenu->CountItems(), true);
            
            const char* jsonModels = nullptr;
            if (msg->FindString("plugin_models", &jsonModels) == B_OK) {
                parse_models_json(jsonModels, fModelMenu);
            }

            if (fModelMenu->CountItems() == 0) {
                fModelMenuField->SetEnabled(false);
                BMenuItem* noModelsItem = new BMenuItem("<No models available>", nullptr);
                noModelsItem->SetEnabled(false);
                fModelMenu->AddItem(noModelsItem);
            } else {
                fModelMenuField->SetEnabled(true);
                fModelMenu->SetTargetForItems(this);

                // RECUPERO IL MODELLO MEMORIZZATO
                AISettings s;
                LoadAISettings(s);

                BMenuItem* savedModelItem = fModelMenu->FindItem(s.model.String());
                if (savedModelItem && s.model.Length() > 0) {
                    savedModelItem->SetMarked(true);
                    fprintf(stderr, "[LOG] Spuntato modello salvato precedentemente: '%s'\n", s.model.String());
                } else {
                    // Fallback sul primo se quello salvato non esiste o non è valido per questo plugin
                    BMenuItem* firstModel = fModelMenu->ItemAt(0);
                    if (firstModel) {
                        firstModel->SetMarked(true);
                        fprintf(stderr, "[LOG] Modello salvato non trovato. Fallback sul primo: '%s'\n", firstModel->Label());
                    }
                }
            }
            fprintf(stderr, "[LOG] --- FINE MSG_MODELS_FETCHED ---\n");
            break;
        }
        case MSG_CONTEXT_SELECTED: {
            _UpdateContextDetails();
            break;
        }
        case MSG_CONTEXT_OPEN: {
            int32 selection = fContextListView->CurrentSelection();
            if (selection < 0) break;
            ContextListItem* item = dynamic_cast<ContextListItem*>(
                fContextListView->ItemAt(selection));
            if (!item) break;

            // Leggi il BMessage flattenato dal file del contesto
            BFile ctxFile(item->FilePath(), B_READ_ONLY);
            if (ctxFile.InitCheck() != B_OK) break;
            BMessage ctx;
            if (ctx.Unflatten(&ctxFile) != B_OK) break;

            // Ricostruisci la conversazione come testo leggibile
            const char* title  = "";
            const char* plugin = "";
            const char* model  = "";
            ctx.FindString("title",       &title);
            ctx.FindString("plugin_name", &plugin);
            ctx.FindString("model_name",  &model);

            BString text;
            text << B_TRANSLATE("Context ID : ") << item->ContextId() << "\n";
            text << B_TRANSLATE("Title      : ") << title  << "\n";
            text << B_TRANSLATE("Plugin     : ") << plugin << "\n";
            text << B_TRANSLATE("Model      : ") << model  << "\n";
            text << "---------------------------------------------\n\n";

            BMessage historyMsg;
            if (ctx.FindMessage("messages", &historyMsg) == B_OK) {
                int32 i = 0;
                BMessage turn;
                while (historyMsg.FindMessage("msg", i++, &turn) == B_OK) {
                    const char* role    = "";
                    const char* content = "";
                    turn.FindString("role",    &role);
                    turn.FindString("content", &content);
                    text << "[" << role << "]\n" << content << "\n\n";
                }
            }
            if (text.IsEmpty())
                text = B_TRANSLATE("[Empty context] -- no messages yet or remote context only\n");

            // Scrivi il file temporaneo
            BPath tmpDir;
            find_directory(B_SYSTEM_TEMP_DIRECTORY, &tmpDir);
            BString tmpName;
            tmpName.SetToFormat("ai_context_%s.txt", item->ContextId());
            BPath tmpPath(tmpDir.Path(), tmpName.String());

            BFile tmpFile(tmpPath.Path(),
                B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
            if (tmpFile.InitCheck() != B_OK) break;
            tmpFile.Write(text.String(), text.Length());

            // Imposta il tipo MIME così si apre con l'editor di testo predefinito
            BNodeInfo nodeInfo(&tmpFile);
            nodeInfo.SetType("text/plain");

            // Apri con l'applicazione associata
            entry_ref ref;
            BEntry tmpEntry(tmpPath.Path());
            if (tmpEntry.GetRef(&ref) == B_OK)
                be_roster->Launch(&ref);
            break;
        }
        case MSG_SESSIONS_FETCHED: {
            fSessionListView->MakeEmpty();
            int32 count = 0;
            msg->FindInt32("count", &count);
            if (count == 0) {
                fSessionListView->AddItem(new BStringItem(B_TRANSLATE("[No active sessions]")));
                break;
            }
            for (int32 i = 0; i < count; i++) {
                BMessage sessionMsg;
                if (msg->FindMessage("session", i, &sessionMsg) != B_OK) continue;

                int32 sid = 0;
                const char* title = "";
                const char* plugin = "";
                const char* model = "";
                sessionMsg.FindInt32("session_id", &sid);
                sessionMsg.FindString("title", &title);
                sessionMsg.FindString("plugin_name", &plugin);
                sessionMsg.FindString("model_name", &model);

                BString line;
                line.SetToFormat("#%" B_PRId32 " | %s  [%s / %s]", sid, title, plugin, model);
                fSessionListView->AddItem(new BStringItem(line.String()));
            }
            break;
        }
        case MSG_CHECK_SESSIONS: {
            _RefreshSessions();
            break;
        }
        case MSG_APPLY: {
            BMessenger server("application/x-vnd.Haiku-ai_server");
            const uint32 MSG_RELOAD_LOCAL = 'RLDS';
            BMessage m(MSG_RELOAD_LOCAL);
            BMessage reply;
            status_t s = server.SendMessage(&m, &reply);
            if (s == B_OK) {
                int32 applied = 0;
                if (reply.FindInt32("applied", &applied) == B_OK) {
                    BString msgStr;
                    msgStr << applied << B_TRANSLATE(" plugin(s) updated.");
                    BAlert* a = new BAlert(B_TRANSLATE("Applied"), msgStr.String(), B_TRANSLATE("OK"));
                    a->Go();
                } else {
                    BAlert* a = new BAlert(B_TRANSLATE("Applied"), B_TRANSLATE("Server reloaded settings."), B_TRANSLATE("OK"));
                    a->Go();
                }
                PostMessage(new BMessage(MSG_REFRESH_PLUGINS));
            } else {
                BAlert* a = new BAlert(B_TRANSLATE("Error"), B_TRANSLATE("Failed to contact ai_server."), B_TRANSLATE("OK"));
                a->Go();
            }
            break;
        }
        case MSG_BASE_URL_OVERRIDE: {
            // Toggle enabling of base URL when override checkbox changed
            if (fBaseUrlOverrideCheckBox) {
                bool ov = (fBaseUrlOverrideCheckBox->Value() == B_CONTROL_ON);
                fBaseUrlControl->SetEnabled(ov);
            }
            break;
        }
        case MSG_SAVE: {
            fprintf(stderr, "\n[LOG] --- OPERAZIONE DI SALVATAGGIO (MSG_SAVE) ---\n");
            AISettings s;
            
            // 1. Recupero Engine (Dalla tendina in alto)
            BMenuItem* engMarked = fEngineMenu->FindMarked();
            if (engMarked) s.engine.SetTo(engMarked->Label());
            else s.engine.SetTo("local");
            
            // 2. Recupero Plugin (Dalla ListView a sinistra)
            int32 selection = fPluginListView->CurrentSelection();
            if (selection >= 0) {
                PluginListItem* item = static_cast<PluginListItem*>(fPluginListView->ItemAt(selection));
                if (item) s.plugin.SetTo(item->Text());
            } else {
                s.plugin.SetTo("");
            }

            // 3. Recupero Modello (Dalla tendina del modello)
            BMenuItem* modMarked = fModelMenu->FindMarked();
            if (modMarked && strcmp(modMarked->Label(), "<No models available>") != 0) {
                s.model.SetTo(modMarked->Label());
            } else {
                s.model.SetTo("");
            }
            
            // 4. Nuovo parametro: Stato del Contesto Remoto
            // Supponendo che la tua struct AISettings supporti un bool o una stringa
            s.use_remote_context = (fRemoteContextCheckBox->Value() == B_CONTROL_ON);

            s.mcp_permissions = 0;
            if (fSystemInfoCheckBox->Value() == B_CONTROL_ON) s.mcp_permissions |= AI_PERM_SYSTEM_INFO;
            if (fFileSystemCheckBox->Value() == B_CONTROL_ON) s.mcp_permissions |= AI_PERM_FILE_SYSTEM;
            if (fRunCommandsCheckBox->Value() == B_CONTROL_ON) s.mcp_permissions |= AI_PERM_RUN_COMMANDS;

            fprintf(stderr, "[LOG] Sto scrivendo su file config:\n  Engine: '%s'\n  Plugin: '%s'\n  Model: '%s'\n", 
                    s.engine.String(), s.plugin.String(), s.model.String());

            // 5. Gestione e validazione API Key (Mantenuta logica originale ottimizzata)
            const char* apiText = fApiKeyControl->Text();
            
            // Controlliamo se l'utente ha lasciato la maschera degli asterischi intatta
            if (strlen(apiText) == 0) {
                s.api_key = "";
            } else if (strlen(apiText) < 16) {
                BAlert* a = new BAlert(B_TRANSLATE("Invalid API Key"), B_TRANSLATE("API key is too short (min 16 chars)."), B_TRANSLATE("OK"));
                a->Go();
                break; // Interrompe il salvataggio se la chiave immessa è invalida
            } else {
                s.api_key.SetTo(apiText);
            }

            // 5b. Base URL (se presente nel controllo)
            const char* baseText = fBaseUrlControl->Text();
            if (baseText && strlen(baseText) > 0) s.base_url.SetTo(baseText);
            else s.base_url.SetTo("");

            s.base_url_override = (fBaseUrlOverrideCheckBox && fBaseUrlOverrideCheckBox->Value() == B_CONTROL_ON);

            // 6. Scrittura effettiva su disco
            if (SaveAISettings(s)) {                
                // Aggiorniamo visivamente l'item nella lista per renderlo l'attuale predefinito
                for (int32 i = 0; i < fPluginListView->CountItems(); i++) {
                    PluginListItem* item = static_cast<PluginListItem*>(fPluginListView->ItemAt(i));
                    if (item) {
                        item->SetDefault(item->Text() == s.plugin);
                    }
                }
                fPluginListView->Invalidate(); // Forza il ridisegno dei colori (Testo Blu)

                fprintf(stderr, "[LOG] Configurazione salvata correttamente su disco.\n");
                BAlert* a = new BAlert(B_TRANSLATE("Saved"), B_TRANSLATE("Settings saved."), B_TRANSLATE("OK"));
                a->Go();
            } else {
                fprintf(stderr, "[LOG] ERRORE durante la scrittura del file di configurazione.\n");
                BAlert* a = new BAlert(B_TRANSLATE("Error"), B_TRANSLATE("Failed to save settings."), B_TRANSLATE("OK"));
                a->Go();
            }
            fprintf(stderr, "[LOG] --- FINE MSG_SAVE ---\n");
            break;
        }
        case MSG_CLEAR_KEY: {
            fprintf(stderr, "\n[LOG] --- CANCELLAZIONE API KEY (MSG_CLEAR_KEY) ---\n");
            
            int32 selection = fPluginListView->CurrentSelection();
            if (selection < 0) {
                fprintf(stderr, "[LOG] Nessun plugin selezionato. Impossibile cancellare la chiave.\n");
                break;
            }

            PluginListItem* item = static_cast<PluginListItem*>(fPluginListView->ItemAt(selection));
            if (!item) break;

            BString currentPlugin = item->Text();

            if (RemoveAPIKey(currentPlugin.String())) {
                fprintf(stderr, "[LOG] API Key rimossa con successo dal KeyStore per il plugin '%s'\n", currentPlugin.String());
                
                fApiKeyControl->SetText("");
                fToggleApiKeyButton->SetLabel(B_TRANSLATE("Show"));

                BAlert* a = new BAlert(B_TRANSLATE("Clear"), B_TRANSLATE("API Key cleared from KeyStore."), B_TRANSLATE("OK"));
                a->Go();
            } else {
                fprintf(stderr, "[LOG] ERRORE durante la rimozione della API Key dal KeyStore.\n");
                BAlert* a = new BAlert(B_TRANSLATE("Error"), B_TRANSLATE("Failed to clear API Key from KeyStore."), B_TRANSLATE("OK"));
                a->Go();
            }
            break;
        }
        case MSG_TOGGLE_KEY: {
        	bool value = fApiKeyControl->IsMasked();
        	const char* label = value ? B_TRANSLATE("Hide") : B_TRANSLATE("Show");
            fApiKeyControl->Mask(!value);
            fToggleApiKeyButton->SetLabel(label);
            break;
        }
        case B_NODE_MONITOR:
            _RefreshContexts();
            break;
        default:
            BWindow::MessageReceived(msg);
    }
}

void PrefletWindow::_UpdatePluginDetails()
{
    int32 selection = fPluginListView->CurrentSelection();
    if (selection < 0) return;

    PluginListItem* item = static_cast<PluginListItem*>(fPluginListView->ItemAt(selection));
    if (!item) return;

    BString pluginName = item->Text();

    // 1. Carica Modelli per il plugin selezionato
    BMessage* threadArgs = new BMessage();
    threadArgs->AddMessenger("reply_to", BMessenger(this));
    threadArgs->AddString("plugin_name", pluginName.String());
    thread_id fetchThread = spawn_thread(FetchModelsThread, "AI Models Fetcher", B_NORMAL_PRIORITY, threadArgs);
    if (fetchThread >= B_OK)
        resume_thread(fetchThread);
    else
        delete threadArgs;

    // 2. Aggiorna API Key e bottoni correlati
    _UpdateApiKeyField();
    _UpdateClearButton();

    // 2b. Aggiorna Base URL, provider e stato override dal settings se presente
    bool isLocalPlugin = (strcmp(item->Type(), "local") == 0);
    
    AISettings s;
    if (LoadAISettings(s) && s.plugin == pluginName) {
        fBaseUrlControl->SetText(s.base_url.String());
        // Override checkbox
        if (fBaseUrlOverrideCheckBox) fBaseUrlOverrideCheckBox->SetValue(s.base_url_override ? B_CONTROL_ON : B_CONTROL_OFF);
    } else {
        fBaseUrlControl->SetText("");
        if (fBaseUrlOverrideCheckBox) fBaseUrlOverrideCheckBox->SetValue(B_CONTROL_OFF);
    }

    // Abilitiamo/disabilitiamo il campo Base URL in base al tipo del plugin (local vs remote)
    if (isLocalPlugin) {
        fBaseUrlControl->SetEnabled(true);
        if (fBaseUrlOverrideCheckBox) {
            fBaseUrlOverrideCheckBox->SetEnabled(false);
            fBaseUrlOverrideCheckBox->SetValue(B_CONTROL_OFF);
        }
    } else {
        if (fBaseUrlOverrideCheckBox) fBaseUrlOverrideCheckBox->SetEnabled(true);
        bool ov = (fBaseUrlOverrideCheckBox && fBaseUrlOverrideCheckBox->Value() == B_CONTROL_ON);
        fBaseUrlControl->SetEnabled(ov);
    }

    // 3. Gestione Checkbox Contesto Remoto
    // Supponiamo che tu abbia una funzione nel Kit o nel config che interroghi il plugin
    //bool supportsRemote = false; // es: AISupportsRemoteContext(pluginName.String());
    //bool usesRemote     = false; // es: AIUsesRemoteContext(pluginName.String());
    uint32 caps = 0;
    caps = AIEngine::GetPluginCapabilities(pluginName.String());

    // Verifichiamo se il bitmask contiene la capacità AI_CAP_REMOTE_CONTEXT (1 << 1)
    bool supportsRemote = (caps & (1 << 1)); 

    if (supportsRemote) {
        fRemoteContextCheckBox->SetEnabled(true);
        
        // Se il plugin supporta il contesto remoto, carichiamo la preferenza salvata su disco
        AISettings s;
        if (LoadAISettings(s)) {
            // Controlliamo se il plugin attualmente selezionato è quello memorizzato come default
            // per evitare di applicare lo stato di un plugin a un altro durante la navigazione della lista
            if (s.plugin == pluginName) {
                fRemoteContextCheckBox->SetValue(s.use_remote_context ? B_CONTROL_ON : B_CONTROL_OFF);
            } else {
                // Se l'utente sta navigando su un altro plugin compatibile ma non predefinito, 
                // lasciamo a false o disattivato di base
                fRemoteContextCheckBox->SetValue(B_CONTROL_OFF);
            }
        } else {
            fRemoteContextCheckBox->SetValue(B_CONTROL_OFF);
        }
    } else {
        // Se il plugin non ha la capacità, spegniamo e azzeriamo la checkbox
        fRemoteContextCheckBox->SetEnabled(false);
        fRemoteContextCheckBox->SetValue(B_CONTROL_OFF);
    }

    // Verifichiamo se il bitmask contiene la capacità AI_CAP_MCP (1 << 3)
    bool supportsMcp = (caps & (1 << 3));

    if (supportsMcp) {
        fSystemInfoCheckBox->SetEnabled(true);
        fFileSystemCheckBox->SetEnabled(true);
        fRunCommandsCheckBox->SetEnabled(true);

        AISettings s;
        if (LoadAISettings(s)) {
            if (s.plugin == pluginName) {
                fSystemInfoCheckBox->SetValue((s.mcp_permissions & AI_PERM_SYSTEM_INFO) ? B_CONTROL_ON : B_CONTROL_OFF);
                fFileSystemCheckBox->SetValue((s.mcp_permissions & AI_PERM_FILE_SYSTEM) ? B_CONTROL_ON : B_CONTROL_OFF);
                fRunCommandsCheckBox->SetValue((s.mcp_permissions & AI_PERM_RUN_COMMANDS) ? B_CONTROL_ON : B_CONTROL_OFF);
            } else {
                fSystemInfoCheckBox->SetValue(B_CONTROL_OFF);
                fFileSystemCheckBox->SetValue(B_CONTROL_OFF);
                fRunCommandsCheckBox->SetValue(B_CONTROL_OFF);
            }
        } else {
            fSystemInfoCheckBox->SetValue(B_CONTROL_OFF);
            fFileSystemCheckBox->SetValue(B_CONTROL_OFF);
            fRunCommandsCheckBox->SetValue(B_CONTROL_OFF);
        }
    } else {
        fSystemInfoCheckBox->SetEnabled(false);
        fFileSystemCheckBox->SetEnabled(false);
        fRunCommandsCheckBox->SetEnabled(false);

        fSystemInfoCheckBox->SetValue(B_CONTROL_OFF);
        fFileSystemCheckBox->SetValue(B_CONTROL_OFF);
        fRunCommandsCheckBox->SetValue(B_CONTROL_OFF);
    }
}

void PrefletWindow::_UpdateContextDetails()
{
    int32 selection = fContextListView->CurrentSelection();
    if (selection < 0) {
        fContextIdView->SetText(B_TRANSLATE("Context ID: None"));
        fContextTextView->SetText("");
        return;
    }

    ContextListItem* item = dynamic_cast<ContextListItem*>(
        fContextListView->ItemAt(selection));
    if (!item) {
        fContextIdView->SetText(B_TRANSLATE("Context ID: None"));
        fContextTextView->SetText("");
        return;
    }

    BString ctxIdText(B_TRANSLATE("Context ID: "));
    ctxIdText.Append(item->ContextId());
    fContextIdView->SetText(ctxIdText.String());

    BFile file(item->FilePath(), B_READ_ONLY);
    if (file.InitCheck() != B_OK) {
        fContextTextView->SetText(B_TRANSLATE("[Error: cannot read context file]"));
        return;
    }

    BMessage ctx;
    if (ctx.Unflatten(&file) != B_OK) {
        fContextTextView->SetText(B_TRANSLATE("[Error: cannot parse context]"));
        return;
    }

    BMessage historyMsg;
    BString summary;
    if (ctx.FindMessage("messages", &historyMsg) == B_OK) {
        int32 i = 0;
        BMessage turn;
        while (historyMsg.FindMessage("msg", i++, &turn) == B_OK) {
            const char* role    = "";
            const char* content = "";
            turn.FindString("role",    &role);
            turn.FindString("content", &content);
            summary << role << ": " << content << "\n\n";
        }
    }

    if (summary.IsEmpty())
        summary = B_TRANSLATE("[Empty context — no messages yet]");

    fContextTextView->SetText(summary.String());
}

void
PrefletWindow::_RefreshContexts()
{
    // Salviamo l'ID del contesto attualmente selezionato per ripristinarlo dopo il refresh
    BString previousId;
    int32 prevSel = fContextListView->CurrentSelection();
    if (prevSel >= 0) {
        ContextListItem* prev = dynamic_cast<ContextListItem*>(
            fContextListView->ItemAt(prevSel));
        if (prev)
            previousId = prev->ContextId();
    }

    fContextListView->MakeEmpty();

    BPath dirPath;
    if (find_directory(B_USER_SETTINGS_DIRECTORY, &dirPath) != B_OK)
        return;
    dirPath.Append("ai_server/contexts");

    BDirectory dir(dirPath.Path());
    if (dir.InitCheck() != B_OK)
        return;

    int32 restoreIndex = -1;
    int32 index = 0;
    BEntry entry;
    while (dir.GetNextEntry(&entry) == B_OK) {
        if (!entry.IsFile()) continue;

        BPath filePath;
        if (entry.GetPath(&filePath) != B_OK) continue;

        BFile file(filePath.Path(), B_READ_ONLY);
        if (file.InitCheck() != B_OK) continue;

        BMessage ctx;
        if (ctx.Unflatten(&file) != B_OK) continue;

        const char* contextId = "";
        const char* title     = "";
        ctx.FindString("context_id", &contextId);
        ctx.FindString("title",      &title);

        BString label;
        if (title && strlen(title) > 0)
            label.SetToFormat("%s  [%s]", title, contextId);
        else
            label.SetTo(contextId);

        fContextListView->AddItem(
            new ContextListItem(label.String(), contextId, filePath.Path()));

        if (!previousId.IsEmpty() && previousId == contextId)
            restoreIndex = index;
        index++;
    }

    if (fContextListView->CountItems() == 0) {
        fContextListView->AddItem(new BStringItem(B_TRANSLATE("[No saved contexts]")));
        fContextIdView->SetText(B_TRANSLATE("Context ID: None"));
        fContextTextView->SetText("");
        return;
    }

    if (restoreIndex >= 0)
        fContextListView->Select(restoreIndex);
}

void
PrefletWindow::_RefreshSessions()
{
    // Lancia un thread asincrono per non bloccare il looper durante l'IPC con ai_server
    BMessenger* messenger = new BMessenger(this);
    thread_id t = spawn_thread(FetchSessionsThread, "AI Sessions Fetcher",
                               B_LOW_PRIORITY, messenger);
    if (t >= B_OK)
        resume_thread(t);
    else
        delete messenger;
}

void
PrefletWindow::_UpdateClearButton()
{
    int32 selection = fPluginListView->CurrentSelection();
    if (selection < 0) {
        fClearApiKeyButton->SetEnabled(false);
        return;
    }

    PluginListItem* item = static_cast<PluginListItem*>(fPluginListView->ItemAt(selection));
    if (item && item->Text() != nullptr && fApiKeyControl->TextLength() > 0) {
        fClearApiKeyButton->SetEnabled(true);
    } else {
        fClearApiKeyButton->SetEnabled(false);
    }
}
void
PrefletWindow::_UpdateApiKeyField()
{
    int32 selection = fPluginListView->CurrentSelection();
    if (selection < 0) {
        //fActualApiKey.SetTo("");
        fApiKeyControl->SetText("");
        return;
    }

    PluginListItem* item = static_cast<PluginListItem*>(fPluginListView->ItemAt(selection));
    if (!item) return;

    BString currentPlugin = item->Text();

    // Recuperiamo la chiave usando la tua funzione nativa dal KeyStore/Config
    BString apiKey;
    if (currentPlugin.Length() > 0 && GetPluginAPIKey(currentPlugin.String(), apiKey)) {
        //if (fApiKeyMasked) {
        //    BString masked;
        //    masked.Append("********", fActualApiKey.Length());
        //    fApiKeyControl->SetText(masked.String());
        //} else {
        //    fApiKeyControl->SetText(fActualApiKey.String());
        //}
        fApiKeyControl->SetText(apiKey.String());
    } else {
        //fActualApiKey.SetTo("");
        fApiKeyControl->SetText("");
    }
}
