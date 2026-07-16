/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef _H
#define _H

#include <vector>
#include <SupportDefs.h>

struct ChatMessage {
    BString role;    // "user" o "assistant" (o "system")
    BString content; // Il testo del messaggio
};

struct ChatContext {
    BString context_id;  // UUID univoco o timestamp (es: "ctx_20260628_1530")
    BString title;       // Titolo della chat (es: "Spiegazione Kernel Haiku")
    BString plugin_name; // Il plugin associato (es: "PublicAI")
    BString model_name;  // Il modello (es: "swiss-ai/apertus-8b-instruct")
    BString remote_id;   // Vuoto se locale, contiene il thread_id se remoto
    
    std::vector<ChatMessage> messages;
};

struct ClientSession {
    int32 id;
    BString plugin_name;
    BString model_name;
    BString custom_api_key;
    bool useCustomAPIKey;
    bool useRemoteContext;   // true = usa OpenAI Assistants API per questo contesto
    BString context_id;
    uint32 mcp_permissions;
    
    BList mpcManager;	// lista di messaggi che contengono le operazioni che può fare l'mpc
    volatile bool abort_requested = false;
};

struct PluginEntry {
    void* dlhandle;
    ai_plugin_t instance;
    std::string path;
    BString type;
    BString name;

    // function pointers resolved from the plugin
    status_t (*generate_sync)(ai_plugin_t, const char*, char*, size_t, BMessage*);
    status_t (*generate_async)(ai_plugin_t, const char*, BMessage*);
    uint32 (*get_capabilities)(void);
    status_t (*list_models)(const BMessage*, char*, size_t);
    status_t (*set_model)(ai_plugin_t, const char*);
    status_t (*update_config)(ai_plugin_t, const BMessage*);
    
};

#endif // _H
