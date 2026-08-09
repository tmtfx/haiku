/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef SRC_KITS_AI_AICOMMANDS_H
#define SRC_KITS_AI_AICOMMANDS_H

#include <String.h>
#include <Message.h>
#include <Messenger.h>
#include <List.h>
#include "AIConfig.h"

// Comandi di Sessione
static const uint32 MSG_OPEN_SESSION		= 'OSES';
static const uint32 MSG_CLOSE_SESSION		= 'CSES';
static const uint32 MSG_GET_SESSION_INFO	= 'GSNF';
static const uint32 MSG_PING				= 'PING';
// Comandi di Generazione / Inferenza
static const uint32 MSG_GEN_SYNC			= 'GENS';
static const uint32 MSG_GEN_ASYNC			= 'GENA';
static const uint32 MSG_ABORT_SESSION		= 'ABRT';
static const uint32 MSG_CHECK_ABORT			= 'CHAB';
// Comandi di Impostazione/Recupero modelli
static const uint32 MSG_LIST				= 'LIST';
static const uint32 MSG_SET_MODEL			= 'SELM';
static const uint32 MSG_RELOAD				= 'RLDS';
static const uint32 MSG_GET_MODELS			= 'GMOD';
static const uint32 MSG_GET_CAPABILITIES	= 'GCAP';
static const uint32 MSG_GET_ALL_SESSIONS    = 'GALS';
// Risposte dal Server
static const uint32 MSG_AI_ACK				= 'ACK_'; // Presa in carico del server
static const uint32 MSG_AI_RESPONSE			= 'ARES'; // Token o risposta asincrona arrivata
static const uint32 MSG_AI_ERROR			= 'ERS '; // Errore interno del server o del plugin
// Contesto/Memoria messaggi
static const uint32 MSG_SWITCH_CONTEXT		= 'SCTX'; // Cambio contesto
// Remote context
static const uint32 MSG_GET_REMOTE_CTX_ID	= 'GRCX'; // Recupera remote_id del contesto corrente
static const uint32 MSG_DELETE_REMOTE_CTX	= 'DRCX'; // Elimina contesto remoto
static const uint32 MSG_SET_REMOTE_CTX		= 'SRCX'; // Abilita/disabilita contesto remoto per una sessione
// MCP
static const uint32 MSG_EXECUTE_TOOL		= 'EXTL'; // Esegue il tool
static const uint32 MSG_MCP_GET_TOOLS		= 'MGTL'; // Ottieni i tool disponibili
static const uint32 MSG_SET_MCP_PERMISSIONS	= 'S_MP'; // Imposta i permessi mcp per la sessione
static const uint32 MSG_GET_MCP_PERMISSIONS	= 'G_MP'; // Ottieni i permessi mcp per la sessione
// System Prompt
static const uint32 MSG_SET_SYSTEM_PROMPT	= 'SSPR'; // Imposta il prompt di sistema per un contesto
static const uint32 MSG_GET_SYSTEM_PROMPT	= 'GSPR'; // Ottiene il prompt di sistema corrente

struct AISessionInfo {
    int32 session_id;
    BString context_id;
    BString title;
    BString plugin_name;
    BString model_name;
    BString base_url;
    BString remote_id;
};


class AIEngine {
public:
    // Inizializza usando le impostazioni globali della Preflet
    AIEngine();
    AIEngine(const char* contextID);
    
    // Inizializza ignorando la Preflet (es. per usare un plugin specifico in un'app)
    //AIEngine(const char* pluginName, const char* modelName, const char* apiKey = nullptr, uint32 mcpPermissions = AI_PERM_SYSTEM_DEFAULT);
    //AIEngine(const char* contextID, const char* pluginName, const char* modelName, const char* apiKey, uint32 mcpPermissions = AI_PERM_SYSTEM_DEFAULT);
    AIEngine(const char* pluginName, const char* modelName, const char* apiKey = nullptr,
            const char* baseUrl = nullptr, uint32 mcpPermissions = AI_PERM_SYSTEM_DEFAULT);
            
    AIEngine(const char* contextID, const char* pluginName, const char* modelName, 
            const char* apiKey, const char* baseUrl, uint32 mcpPermissions = AI_PERM_SYSTEM_DEFAULT);
    ~AIEngine();

    // Comandi Semplificati (Sincroni)
    status_t    Generate(const char* prompt, BString& outResponse);
    status_t    GenerateAsync(const char* prompt, BMessenger target);
    status_t    GetStatus(BString& outStatus);
    status_t	SetContext(const char* contextID);
    status_t    GetContextID(BString& outContextID) const;
    status_t    GetTitle(BString& outTitle) const;

    // System Prompt
    status_t    SetSystemPrompt(const char* systemPrompt);
    status_t    GetSystemPrompt(BString& outSystemPrompt) const;

    // Remote context
    void        EnableRemoteContext(bool enable);
    status_t    GetRemoteContextId(BString& outRemoteId) const;


    // Permette di cambiare al volo la configurazione di questa istanza
    void        SetPlugin(const char* pluginName);
    void        SetModel(const char* modelName);
    void        SetApiKey(const char* apiKey);
    void        SetBaseUrl(const char* baseUrl);
    status_t	SetMCPPermissions(uint32 permissions);
    uint32		GetMCPPermissions();
    
    static uint32		GetPluginCapabilities(const char* pluginName);
    static status_t		GetAllSessions(BList& outSessionsList);

private:
    status_t    _EnsureServerRunning();
    status_t    _TalkToServer(BMessage* message, BMessage& reply, bigtime_t timeout = 15000000);

    BString     fPlugin;
    BString     fModel;
    BString     fApiKey;
    BString     fBaseUrl;
    BMessenger  fServerMessenger;
    bool        fUseSystemSettings;
    bool        fUseRemoteContext;
    int32    fSessionID;
    BString     fContextID;
};

#endif // SRC_KITS_AI_AICOMMANDS_H
