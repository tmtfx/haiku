/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include "AIConfig.h"
#include "AICommands.h"
#include <Roster.h>
#include <Errors.h>
#include <unistd.h>
#include <cstdio>

static const char* kServerSignature = "application/x-vnd.Haiku-ai_server";

AIEngine::AIEngine()
    : fPlugin(""),
      fModel(""),
      fApiKey(""),
      fBaseUrl(""),
      fUseSystemSettings(true),
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
        const char* serverCtx = nullptr;
        if (reply.FindString("context_id", &serverCtx) == B_OK) {
            fContextID = serverCtx;
        }
    }
}

// Costruttore per riprendere un contesto esistente
AIEngine::AIEngine(const char* contextID)
    : fPlugin(""),
      fModel(""),
      fApiKey(""),
      fBaseUrl(""),
      fUseSystemSettings(true), 
      fUseRemoteContext(false),
      fSessionID(-1), 
      fContextID(contextID ? contextID : "")
{
    BMessage msg(MSG_OPEN_SESSION);
    msg.AddString("context_id", fContextID);
    AISettings settings;
    if (LoadAISettings(settings)) {
        msg.AddInt32("mcp_permissions", (int32)settings.mcp_permissions);
    }
    
    BMessage reply;
    if (_TalkToServer(&msg, reply) == B_OK) {
        reply.FindInt32("session_id", &fSessionID);
        const char* serverCtx = nullptr;
        if (reply.FindString("context_id", &serverCtx) == B_OK) {
            fContextID = serverCtx;
        }
    }
}

AIEngine::AIEngine(const char* pluginName, const char* modelName, const char* apiKey, 
                   const char* baseUrl, uint32 mcpPermissions)
    : fPlugin(pluginName ? pluginName : ""),
      fModel(modelName ? modelName : ""),
      fApiKey(apiKey ? apiKey : ""),
      fBaseUrl(baseUrl ? baseUrl : ""),
      fUseSystemSettings(false),
      fUseRemoteContext(false),
      fSessionID(-1),
      fContextID("")
{
    BMessage msg(MSG_OPEN_SESSION);
    if (fPlugin.Length() > 0) msg.AddString("plugin", fPlugin);
    if (fModel.Length() > 0)  msg.AddString("model", fModel);
    if (fApiKey.Length() > 0)  msg.AddString("api_key", fApiKey);
    if (fBaseUrl.Length() > 0) msg.AddString("base_url", fBaseUrl);

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
        const char* serverCtx = nullptr;
        if (reply.FindString("context_id", &serverCtx) == B_OK) {
            fContextID = serverCtx;
        }
    }
}

AIEngine::AIEngine(const char* contextID, const char* pluginName, const char* modelName, 
                   const char* apiKey, const char* baseUrl, uint32 mcpPermissions)
    : fPlugin(pluginName ? pluginName : ""),
      fModel(modelName ? modelName : ""),
      fApiKey(apiKey ? apiKey : ""),
      fBaseUrl(baseUrl ? baseUrl : ""),
      fUseSystemSettings(false),
      fUseRemoteContext(false),
      fSessionID(-1),
      fContextID(contextID ? contextID : "")
{
    BMessage msg(MSG_OPEN_SESSION);
    if (fPlugin.Length() > 0) msg.AddString("plugin", fPlugin);
    if (fModel.Length() > 0)  msg.AddString("model", fModel);
    if (fContextID.Length() > 0) msg.AddString("context_id", fContextID);
    if (fApiKey.Length() > 0)  msg.AddString("api_key", fApiKey);
    if (fBaseUrl.Length() > 0) msg.AddString("base_url", fBaseUrl);

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
        const char* serverCtx = nullptr;
        if (reply.FindString("context_id", &serverCtx) == B_OK) {
            fContextID = serverCtx;
        }
    }
}

AIEngine::~AIEngine()
{
    if (fSessionID != -1) {
        BMessage msg(MSG_CLOSE_SESSION);
        msg.AddInt32("session_id", fSessionID);
        fServerMessenger.SendMessage(&msg);
    }
}

void AIEngine::SetPlugin(const char* pluginName) { fPlugin = pluginName ? pluginName : ""; fUseSystemSettings = false; }
void AIEngine::SetModel(const char* modelName)   { fModel = modelName ? modelName : ""; fUseSystemSettings = false; }
void AIEngine::SetApiKey(const char* apiKey)     { fApiKey = apiKey ? apiKey : ""; fUseSystemSettings = false; }
void AIEngine::SetBaseUrl(const char* baseUrl)   { fBaseUrl = baseUrl ? baseUrl : ""; fUseSystemSettings = false; }

void AIEngine::EnableRemoteContext(bool enable)
{
    fUseRemoteContext = enable;
    if (fSessionID == -1) return;

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
    status_t err = const_cast<AIEngine*>(this)->_TalkToServer(&msg, reply);
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
        status_t res = be_roster->Launch(kServerSignature);
        if (res != B_OK && res != B_ALREADY_RUNNING)
            return res;

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

    if (!fUseSystemSettings) {
        if (fPlugin.Length() > 0)  message->AddString("custom_plugin", fPlugin.String());
        if (fModel.Length() > 0)   message->AddString("custom_model", fModel.String());
        if (fApiKey.Length() > 0)  message->AddString("custom_api_key", fApiKey.String());
        if (fBaseUrl.Length() > 0) message->AddString("custom_base_url", fBaseUrl.String());
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
    status_t res = _TalkToServer(&msg, reply, 60000000); // Portato a 60 secondi per LLM locali lenti
    if (res != B_OK)
        return res;

    const char* responseText = nullptr;
    if (reply.FindString("response", &responseText) == B_OK) {
        outResponse.SetTo(responseText);
        return B_OK;
    }

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

    BMessage msg(MSG_GEN_ASYNC);
    msg.AddString("prompt", prompt);
    msg.AddInt32("session_id", fSessionID);
    msg.AddMessenger("target", target);

    BMessage ack;
    status_t err = _TalkToServer(&msg, ack, 5000000);
    if (err != B_OK)
        return err;

    const char* status = nullptr;
    if (ack.FindString("status", &status) == B_OK && strcmp(status, "ok") == 0) {
        return B_OK;
    }

    return B_ERROR;
}

status_t
AIEngine::Abort()
{
	BMessage msg(MSG_ABORT_SESSION);
	msg.AddInt32("session_id", fSessionID);
	
	BMessage ack;
    status_t err = _TalkToServer(&msg, ack, 5000000);
    if (err != B_OK)
        return err;

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
    BMessage msg(MSG_GET_SESSION_INFO);
    msg.AddInt32("session_id", fSessionID);
    msg.AddString("context_id", fContextID);
    
    BMessage reply;
    if (const_cast<AIEngine*>(this)->_TalkToServer(&msg, reply) == B_OK) {
        if (reply.FindString("title", &outTitle) == B_OK) {
            return B_OK;
        }
    }
    outTitle = "Nuova Conversazione";
    return B_OK;
}

status_t
AIEngine::SetSystemPrompt(const char* systemPrompt)
{
    BMessage msg(MSG_SET_SYSTEM_PROMPT);
    msg.AddInt32("session_id", fSessionID);
    msg.AddString("context_id", fContextID);
    msg.AddString("system_prompt", systemPrompt ? systemPrompt : "");

    BMessage reply;
    status_t rc = _TalkToServer(&msg, reply);
    if (rc == B_OK) {
        int32 status = B_ERROR;
        if (reply.FindInt32("status", &status) == B_OK) {
            return status;
        }
    }
    return rc;
}

status_t
AIEngine::GetSystemPrompt(BString& outSystemPrompt) const
{
    BMessage msg(MSG_GET_SYSTEM_PROMPT);
    msg.AddInt32("session_id", fSessionID);
    msg.AddString("context_id", fContextID);

    BMessage reply;
    status_t rc = const_cast<AIEngine*>(this)->_TalkToServer(&msg, reply);
    if (rc == B_OK) {
        const char* systemPrompt = nullptr;
        if (reply.FindString("system_prompt", &systemPrompt) == B_OK) {
            outSystemPrompt.SetTo(systemPrompt);
            return B_OK;
        }
    }
    return rc;
}

/*static*/ uint32
AIEngine::GetPluginCapabilities(const char* pluginName)
{
    if (pluginName == nullptr || strlen(pluginName) == 0)
        return 0;

    AIEngine tmpEngine; // Usa un'istanza temporanea per garantire l'avvio e la connessione via _TalkToServer
    BMessage request(MSG_GET_CAPABILITIES);
    request.AddString("plugin_name", pluginName);

    BMessage reply;
    if (tmpEngine._TalkToServer(&request, reply, 2000000) != B_OK)
        return 0;

    uint32 capabilities = 0;
    if (reply.FindInt32("capabilities", (int32*)&capabilities) == B_OK) {
        return capabilities;
    }

    return 0;
}

/*static*/ status_t
AIEngine::GetAllSessions(BList& outSessionsList)
{
    AIEngine tmpEngine;
    BMessage request(MSG_GET_ALL_SESSIONS);
    BMessage reply;

    status_t err = tmpEngine._TalkToServer(&request, reply, 2000000);
    if (err != B_OK) return err;

    int32 count = 0;
    reply.FindInt32("count", &count);

    for (int32 i = 0; i < count; i++) {
        BMessage sessionInfo;
        if (reply.FindMessage("session", i, &sessionInfo) == B_OK) {
            AISessionInfo* info = new AISessionInfo();
            sessionInfo.FindInt32("session_id", &info->session_id);
            sessionInfo.FindString("context_id", &info->context_id);
            sessionInfo.FindString("title", &info->title);
            sessionInfo.FindString("plugin_name", &info->plugin_name);
            sessionInfo.FindString("model_name", &info->model_name);
            sessionInfo.FindString("base_url", &info->base_url);
            sessionInfo.FindString("remote_id", &info->remote_id);

            outSessionsList.AddItem(info);
        }
    }

    return B_OK;
}

status_t AIEngine::SetMCPPermissions(uint32 permissions)
{
    BMessage request(MSG_SET_MCP_PERMISSIONS);
    request.AddUInt32("permissions", permissions);
    request.AddInt32("session_id", fSessionID);
    BMessage reply;

    status_t err = _TalkToServer(&request, reply, 2000000);
    if (err != B_OK) return err;
    
    reply.FindInt32("status", &err);
    return err;
}

uint32 AIEngine::GetMCPPermissions()
{
    BMessage request(MSG_GET_MCP_PERMISSIONS);
    request.AddInt32("session_id", fSessionID);
    BMessage reply;

    status_t err = _TalkToServer(&request, reply, 2000000);
    if (err != B_OK) {
        fprintf(stderr, "[libai_api] Errore richiesta permessi ad ai_server\n");
        return 0;
    }
    
    uint32 perm = 0;
    if (reply.FindUInt32("permissions", &perm) == B_OK)
        return perm;
    return 0;
}
