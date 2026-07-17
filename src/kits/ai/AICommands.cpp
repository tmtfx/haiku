/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include "AICommands.h"
#include <Roster.h>
#include <Errors.h>
#include <unistd.h>
#include <cstdio>


static const char* kServerSignature = "application/x-vnd.Haiku-ai_server";

AIEngine::AIEngine()
    : fUseSystemSettings(true),
      fUseRemoteContext(false),
      fSessionID(-1),
      fContextID("")
{
	BMessage msg(MSG_OPEN_SESSION);
    AISettings settings;
    if (LoadAISettings(settings)) {
        msg.AddInt32("mcp_permissions", (int32)settings.mcp_permissions);
    }
    BMessage reply;
    
    if (_TalkToServer(&msg, reply) == B_OK) {
        reply.FindInt32("session_id", &fSessionID);
        //reply.FindString("context_id", &fContextID); // Il server ne genera uno nuovo nativo
        const char* serverCtx = nullptr;
        if (reply.FindString("context_id", &serverCtx) == B_OK) {
            fContextID = serverCtx; // BString copia il testo restituito dal server
        }
    }
}
// 2. Costruttore per riprendere un contesto esistente (o forzarne uno specifico)
AIEngine::AIEngine(const char* contextID)
    : fUseSystemSettings(true), fUseRemoteContext(false),
      fSessionID(-1), fContextID(contextID ? contextID : "")
{
    BMessage msg(MSG_OPEN_SESSION);
    msg.AddString("context_id", fContextID); // Comunichiamo al server che vogliamo QUESTO contesto
    AISettings settings;
    if (LoadAISettings(settings)) {
        msg.AddInt32("mcp_permissions", (int32)settings.mcp_permissions);
    }
    
    BMessage reply;
    if (_TalkToServer(&msg, reply) == B_OK) {
        reply.FindInt32("session_id", &fSessionID);
        // Il server risponderà configurando la sessione con il plugin/modello 
        // salvati dentro quel contesto specifico!
        const char* serverCtx = nullptr;
        if (reply.FindString("context_id", &serverCtx) == B_OK) {
            fContextID = serverCtx; // BString copia il testo restituito dal server
        }
    }
}

AIEngine::AIEngine(const char* pluginName, const char* modelName, const char* apiKey, uint32 mcpPermissions)
    : fPlugin(pluginName),
      fModel(modelName),
      fApiKey(apiKey ? apiKey : ""),
      fUseSystemSettings(false),
      fUseRemoteContext(false),
      fSessionID(-1),
      fContextID("")
{
	BMessage msg(MSG_OPEN_SESSION);
    msg.AddString("plugin", fPlugin);
    msg.AddString("model", fModel);
    if (fApiKey.Length() > 0) msg.AddString("api_key", fApiKey);
    if (mcpPermissions != AI_PERM_SYSTEM_DEFAULT) {
        msg.AddInt32("mcp_permissions", (int32)mcpPermissions);
    } else {
        AISettings settings;
        if (LoadAISettings(settings)) {
            msg.AddInt32("mcp_permissions", (int32)settings.mcp_permissions);
        }
    }
    
    BMessage reply;
    if (_TalkToServer(&msg, reply) == B_OK) {
        reply.FindInt32("session_id", &fSessionID);
        //reply.FindString("context_id", &fContextID); // Il server ne genera uno nuovo nativo
        const char* serverCtx = nullptr;
        if (reply.FindString("context_id", &serverCtx) == B_OK) {
            fContextID = serverCtx; // BString copia il testo restituito dal server
        }
    }
}

AIEngine::AIEngine(const char* contextID, const char* pluginName, const char* modelName, const char* apiKey, uint32 mcpPermissions)
    : fPlugin(pluginName),
      fModel(modelName),
      fApiKey(apiKey ? apiKey : ""),
      fUseSystemSettings(false),
      fUseRemoteContext(false),
      fSessionID(-1),
      fContextID(contextID ? contextID : "")
{
	BMessage msg(MSG_OPEN_SESSION);
    msg.AddString("plugin", fPlugin);
    msg.AddString("model", fModel);
    msg.AddString("context_id", fContextID);
    if (fApiKey.Length() > 0) msg.AddString("api_key", fApiKey);
    if (mcpPermissions != AI_PERM_SYSTEM_DEFAULT) {
        msg.AddInt32("mcp_permissions", (int32)mcpPermissions);
    } else {
        AISettings settings;
        if (LoadAISettings(settings)) {
            msg.AddInt32("mcp_permissions", (int32)settings.mcp_permissions);
        }
    }
    
    BMessage reply;
    if (_TalkToServer(&msg, reply) == B_OK) {
        reply.FindInt32("session_id", &fSessionID);
        //reply.FindString("context_id", &fContextID); // Il server ne genera uno nuovo nativo
        const char* serverCtx = nullptr;
        if (reply.FindString("context_id", &serverCtx) == B_OK) {
            fContextID = serverCtx; // BString copia il testo restituito dal server
        }
    }
}

AIEngine::~AIEngine()
{
	if (fSessionID != -1) {
        BMessage msg(MSG_CLOSE_SESSION);
        msg.AddInt32("session_id", fSessionID);
        
        // Invio rapido asincrono senza attesa di risposta (tanto stiamo chiudendo)
        fServerMessenger.SendMessage(&msg);
    }
}

void AIEngine::SetPlugin(const char* pluginName) { fPlugin = pluginName; fUseSystemSettings = false; }
void AIEngine::SetModel(const char* modelName)  { fModel = modelName; fUseSystemSettings = false; }
void AIEngine::SetApiKey(const char* apiKey)   { fApiKey = apiKey; fUseSystemSettings = false; }

void AIEngine::EnableRemoteContext(bool enable)
{
    fUseRemoteContext = enable;

    if (fSessionID == -1) return; // sessione non ancora aperta

    BMessage msg(MSG_SET_REMOTE_CTX);
    msg.AddInt32("session_id", fSessionID);
    msg.AddBool("enable", enable);

    BMessage reply;
    _TalkToServer(&msg, reply);
}

status_t
AIEngine::GetRemoteContextId(BString& outRemoteId) const
{
    BMessage msg(MSG_GET_REMOTE_CTX_ID);
    msg.AddString("context_id", fContextID.String());
    if (fSessionID != -1)
        msg.AddInt32("session_id", fSessionID);

    BMessage reply;
    // _TalkToServer non è const, usiamo il messenger direttamente
    BMessenger server(kServerSignature);
    if (!server.IsValid()) return B_NO_INIT;
    status_t err = server.SendMessage(&msg, &reply, 10000000);
    if (err != B_OK) return err;

    const char* remoteId = nullptr;
    if (reply.FindString("remote_id", &remoteId) == B_OK && remoteId) {
        outRemoteId = remoteId;
        return B_OK;
    }
    return B_NAME_NOT_FOUND;
}

status_t
AIEngine::_EnsureServerRunning()
{
    if (fServerMessenger.IsValid())
        return B_OK;

    fServerMessenger = BMessenger(kServerSignature);
    if (!fServerMessenger.IsValid()) {
        // Il server è spento, proviamo a lanciarlo tramite il Roster di Haiku
        status_t res = be_roster->Launch(kServerSignature);
        if (res != B_OK)
            return res;

        // Diamo al server un momento per fare l'init (fino a 2 secondi)
        for (int i = 0; i < 10; i++) {
            usleep(200000); // 200ms
            fServerMessenger = BMessenger(kServerSignature);
            if (fServerMessenger.IsValid())
                return B_OK;
        }
    }

    return fServerMessenger.IsValid() ? B_OK : B_TIMED_OUT;
}

status_t
AIEngine::_TalkToServer(BMessage* message, BMessage& reply, bigtime_t timeout)
{
    status_t err = _EnsureServerRunning();
    if (err != B_OK)
        return err;

    // Se l'applicazione sta usando una configurazione personalizzata, 
    // la iniettiamo nel messaggio in modo che l'ai_server sappia di non dover usare i suoi default.
    if (!fUseSystemSettings) {
        message->AddString("custom_plugin", fPlugin.String());
        message->AddString("custom_model", fModel.String());
        if (fApiKey.Length() > 0)
            message->AddString("custom_api_key", fApiKey.String());
    }

    return fServerMessenger.SendMessage(message, &reply, timeout);
}

status_t AIEngine::SetContext(const char* contextID)
{
    BMessage msg(MSG_SWITCH_CONTEXT);
    msg.AddInt32("session_id", fSessionID);
    msg.AddString("context_id", contextID);
    
    BMessage reply;
    status_t rc = _TalkToServer(&msg, reply);
    if (rc == B_OK) {
        fContextID = contextID;
    }
    return rc;
}

status_t
AIEngine::Generate(const char* prompt, BString& outResponse)
{
    if (!prompt) return B_BAD_VALUE;

    BMessage msg(MSG_GEN_SYNC);
    msg.AddString("prompt", prompt);
    msg.AddInt32("session_id", fSessionID);

    BMessage reply;
    // Timeout generoso di 25 secondi per le generazioni pesanti o remote
    status_t res = _TalkToServer(&msg, reply, 25000000); 
    if (res != B_OK)
        return res;

    const char* responseText = nullptr;
    if (reply.FindString("response", &responseText) == B_OK) {
        outResponse.SetTo(responseText);
        return B_OK;
    }

    // Se il server ha risposto ma c'è un errore interno dell'IA, lo intercettiamo
    const char* errorText = nullptr;
    if (reply.FindString("error", &errorText) == B_OK) {
        outResponse.SetTo(errorText);
        return B_ERROR;
    }

    return B_BAD_DATA;
}

status_t
AIEngine::GenerateAsync(const char* prompt, BMessenger target)
{
    if (!prompt || strlen(prompt) == 0)
        return B_BAD_VALUE;

    if (!target.IsValid())
        return B_BAD_VALUE;

    // Assicuriamoci che il demone ai_server sia attivo prima di inviare
    status_t err = _EnsureServerRunning();
    if (err != B_OK)
        return err;

    // Prepariamo il messaggio asincrono ('GENA')
    BMessage msg(MSG_GEN_ASYNC);
    msg.AddString("prompt", prompt);
    msg.AddInt32("session_id", fSessionID);
    
    // Consegniamo al server il BMessenger a cui dovrà recapitare i token ('ARES')
    msg.AddMessenger("target", target);

    // Se l'istanza non usa i setting globali, specifichiamo il plugin/modello desiderato
    if (!fUseSystemSettings) {
        if (fPlugin.Length() > 0) msg.AddString("plugin", fPlugin.String());
        if (fModel.Length() > 0)   msg.AddString("model", fModel.String());
    }

    // Inviamo la richiesta in modalità asincrona.
    // Usiamo una SendMessage normale senza aspettare una risposta complessa nel thread corrente,
    // oppure gestiamo l'ACK sincrono immediato del server.
    BMessage ack;
    err = _TalkToServer(&msg, ack, 5000000); // 5 secondi di timeout per l'ACK iniziale
    if (err != B_OK)
        return err;

    // Verifichiamo se il server ha accettato la presa in carico (ACK)
    const char* status = nullptr;
    if (ack.FindString("status", &status) == B_OK && strcmp(status, "ok") == 0) {
        return B_OK;
    }

    return B_ERROR;
}

status_t
AIEngine::GetStatus(BString& outStatus)
{
    BMessage msg(MSG_PING);
    BMessage reply;
    
    status_t res = _TalkToServer(&msg, reply, 5000000);
    if (res != B_OK)
        return res;

    const char* statusText = nullptr;
    if (reply.FindString("status", &statusText) == B_OK) {
        outStatus.SetTo(statusText);
        return B_OK;
    }

    return B_BAD_DATA;
}

status_t AIEngine::GetContextID(BString& outContextID) const
{
    if (fContextID.IsEmpty()) return B_ERROR;
    outContextID = fContextID;
    return B_OK;
}

status_t AIEngine::GetTitle(BString& outTitle) const
{
    // Mandiamo un messaggio veloce al server chiedendo le info della sessione o del contesto attivo
    BMessage msg(MSG_GET_SESSION_INFO); // Definisci questo what se manca
    msg.AddInt32("session_id", fSessionID);
    msg.AddString("context_id", fContextID);
    
    BMessage reply;
    if (const_cast<AIEngine*>(this)->_TalkToServer(&msg, reply) == B_OK) {
        if (reply.FindString("title", &outTitle) == B_OK) {
            return B_OK;
        }
    }
    outTitle = "Nuova Conversazione"; // Fallback se il server non ha ancora un titolo
    return B_OK;
}


/*static*/ uint32
AIEngine::GetPluginCapabilities(const char* pluginName)
{
    if (pluginName == nullptr || strlen(pluginName) == 0)
        return 0;

    // Connessione diretta e pulita al server senza toccare le sessioni
    BMessenger server(kServerSignature); 
    if (!server.IsValid()) {
        fprintf(stderr, "[libai_api] Errore: ai_server non raggiungibile.\n");
        return 0;
    }

    BMessage request(MSG_GET_CAPABILITIES);
    request.AddString("plugin_name", pluginName);

    BMessage reply;
    // Timeout di 2 secondi
    status_t err = server.SendMessage(&request, &reply, 2000000, 2000000); 
    if (err != B_OK) {
        fprintf(stderr, "[libai_api] Errore IPC GetPluginCapabilities: %s\n", strerror(err));
        return 0;
    }

    uint32 capabilities = 0;
    if (reply.FindInt32("capabilities", (int32*)&capabilities) == B_OK) {
        return capabilities;
    }

    return 0;
}
/*static*/ status_t
AIEngine::GetAllSessions(BList& outSessionsList)
{
    // Per sicurezza, svuotiamo la lista passata (occhio ai memory leak se conteneva già roba)
    // In alternativa, assumiamo che sia vuota. Aqui la popoliamo e basta.
    
    BMessenger server(kServerSignature);
    if (!server.IsValid())
        return B_SERVER_NOT_FOUND;

    BMessage request(MSG_GET_ALL_SESSIONS);
    BMessage reply;

    status_t err = server.SendMessage(&request, &reply, 2000000, 2000000);
    if (err != B_OK) return err;

    int32 count = 0;
    reply.FindInt32("count", &count);

    for (int32 i = 0; i < count; i++) {
        BMessage sessionInfo;
        if (reply.FindMessage("session", i, &sessionInfo) == B_OK) {
            // Allochiamo dinamicamente la struct sul heap
            AISessionInfo* info = new AISessionInfo();
            
            sessionInfo.FindInt32("session_id", &info->session_id);
            sessionInfo.FindString("context_id", &info->context_id);
            sessionInfo.FindString("title", &info->title);
            sessionInfo.FindString("plugin_name", &info->plugin_name);
            sessionInfo.FindString("model_name", &info->model_name);

            // Aggiungiamo il puntatore alla BList
            outSessionsList.AddItem(info);
        }
    }

    return B_OK;
}
