// deepseek_plugin.cpp
// Plugin DeepSeek per Haiku - implementazione OpenAI-compatible unificata con streaming SSE e gestione errori.

#include <os/ai/AIPlugin.h>
#include <os/ai/AINetworkPlugin.h>
#include <os/ai/AICommands.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <Url.h>
#include <UrlProtocolRoster.h>
#include <UrlRequest.h>
#include <UrlSynchronousRequest.h>
#include <DataIO.h>
#include <HttpHeaders.h>
#include <HttpRequest.h>
#include <String.h>
#include <File.h>
#include <OS.h>

#include <Json.h>
#include <HttpResult.h>

using namespace BPrivate::Network;

#define DEFAULT_DEEPSEEK_URL   "https://api.deepseek.com"
#define DEFAULT_DEEPSEEK_MODEL "deepseek-chat"
#define DEEPSEEK_USER_AGENT    "HaikuAIEngine/1.0"

static char* dupstr_or_null(const char* s)
{
    if (!s)
        return nullptr;
    size_t n = strlen(s) + 1;
    char* p = (char*)malloc(n);
    if (p)
        memcpy(p, s, n);
    return p;
}

static BString EscapeStringForJson(const char* input) {
    if (!input) return "";
    BString escaped;
    const char* p = input;
    while (*p) {
        switch (*p) {
            case '\\': escaped << "\\\\"; break;
            case '"':  escaped << "\\\""; break;
            case '\b': escaped << "\\b";  break;
            case '\f': escaped << "\\f";  break;
            case '\n': escaped << "\\n";  break;
            case '\r': escaped << "\\r";  break;
            case '\t': escaped << "\\t";  break;
            default:
                if ((unsigned char)*p < 0x20) {
                    char buf[7];
                    snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)*p);
                    escaped << buf;
                } else {
                    escaped << *p;
                }
                break;
        }
        p++;
    }
    return escaped;
}

static status_t CopyToBuffer(const char* text, char* out, size_t outLen)
{
    if (out == NULL || outLen == 0)
        return B_BAD_VALUE;

    if (text == NULL)
        text = "";

    size_t length = strlen(text);
    size_t copyLength = length < outLen - 1 ? length : outLen - 1;
    memcpy(out, text, copyLength);
    out[copyLength] = '\0';
    return B_OK;
}

void SerializeBMessageToJson(const BMessage* msg, BString& outJson) {
    outJson << "{";
    bool first = true;
    
    char* field_name = nullptr;
    uint32 field_code = 0;
    int32 field_count = 0;
    
    for (int which = 0; msg->GetInfo(B_ANY_TYPE, which, 
            (char**)&field_name, &field_code, &field_count) == B_OK; which++) {
        
        if (!first) outJson << ",";
        first = false;
        
        outJson << "\"" << field_name << "\":";
        
        if (field_code == B_MESSAGE_TYPE) {
            BMessage subMsg;
            if (msg->FindMessage(field_name, &subMsg) == B_OK) {
                BString subJson;
                SerializeBMessageToJson(&subMsg, subJson);
                outJson << subJson;
            } else {
                outJson << "{}";
            }
        } else if (field_code == B_STRING_TYPE || field_code == B_CHAR_TYPE) {
            const char* strVal = msg->FindString(field_name);
            if (strVal) {
                BString escaped = EscapeStringForJson(strVal);
                outJson << "\"" << escaped << "\"";
            } else {
                outJson << "\"\"";
            }
        } else if (field_code == B_BOOL_TYPE) {
            bool boolVal = false;
            msg->FindBool(field_name, &boolVal);
            outJson << (boolVal ? "true" : "false");
        } else {
            int32 intVal = 0;
            msg->FindInt32(field_name, &intVal);
            outJson << intVal;
        }
    }
    
    outJson << "}";
}

void ConvertBMessageToOpenAIToolsJson(const BMessage* toolsMsg, BString& outJson)
{
    outJson = "[";

    BMessage tool;
    int32 i = 0;
    bool first = true;

    while (toolsMsg->FindMessage("tool", i, &tool) == B_OK) {
        if (!first) outJson << ",";
        first = false;

        const char* name = tool.FindString("name");
        const char* desc = tool.FindString("description");

        BString escapedDesc = EscapeStringForJson(desc);

        outJson << "{";
        outJson << "\"type\":\"function\",";
        outJson << "\"function\":{";
        outJson << "\"name\":\"" << name << "\",";
        outJson << "\"description\":\"" << escapedDesc << "\"";

        BMessage params;
        tool.FindMessage("parameters", &params);
        if (!params.IsEmpty()) {
            BMessage requiredMsg;
            BString reqFieldStr = "";
            const char* fStr = nullptr;
            if (params.FindString("required", &fStr) == B_OK && fStr != nullptr) {
                reqFieldStr = fStr;
            } else if (params.FindMessage("required", &requiredMsg) == B_OK) {
                const char* f = requiredMsg.FindString("0");
                if (f) reqFieldStr = f;
            }
            params.RemoveName("required");

            BString jsonParams;
            SerializeBMessageToJson(&params, jsonParams);
            
            if (!reqFieldStr.IsEmpty()) {
                int32 lastBrace = jsonParams.FindLast("}");
                if (lastBrace != B_ERROR) {
                    jsonParams.Truncate(lastBrace);
                    jsonParams << ",\"required\":[\"" << reqFieldStr << "\"]}";
                }
            }

            outJson << ",\"parameters\":" << jsonParams;
        } else {
            outJson << ",\"parameters\":{\"type\":\"object\",\"properties\":{}}";
        }

        outJson << "}"; // fine function
        outJson << "}"; // fine tool
        i++;
    }

    outJson << "]";
}

void AppendToolCallToContext(BMessage* context, const char* name, const BMessage* argsMsg, const char* toolCallId) {
    BMessage messagesMsg;
    if (context->FindMessage("messages", &messagesMsg) != B_OK) {
        context->AddMessage("messages", &messagesMsg);
    }
    int32 i = 0; BMessage dummy;
    while (messagesMsg.FindMessage("msg", i, &dummy) == B_OK) i++;
    
    BMessage toolCallMsg;
    toolCallMsg.AddString("type", "functionCall");
    toolCallMsg.AddString("name", name);
    if (argsMsg) {
        toolCallMsg.AddMessage("args", argsMsg);
    }
    if (toolCallId) {
        toolCallMsg.AddString("tool_call_id", toolCallId);
    }
    
    messagesMsg.AddMessage("msg", &toolCallMsg);
    context->RemoveName("messages");
    context->AddMessage("messages", &messagesMsg);
}

void AppendToolResponseToContext(BMessage* context, const char* name, const char* responseJson, const char* toolCallId) {
    BMessage messagesMsg;
    if (context->FindMessage("messages", &messagesMsg) != B_OK) {
        context->AddMessage("messages", &messagesMsg);
    }
    int32 i = 0; BMessage dummy;
    while (messagesMsg.FindMessage("msg", i, &dummy) == B_OK) i++;
    
    BMessage toolRespMsg;
    toolRespMsg.AddString("type", "functionResponse");
    toolRespMsg.AddString("name", name);
    toolRespMsg.AddString("response", responseJson);
    if (toolCallId) {
        toolRespMsg.AddString("tool_call_id", toolCallId);
    }
    
    messagesMsg.AddMessage("msg", &toolRespMsg);
    context->RemoveName("messages");
    context->AddMessage("messages", &messagesMsg);
}

static void
BuildPayloadFromContext(const BMessage* config, const char* currentPrompt,
    BString& outPayload, bool stream)
{
    const char* model = nullptr;
    if (config)
        config->FindString("model_name", &model);
    if (!model || model[0] == '\0')
        model = DEFAULT_DEEPSEEK_MODEL;

    outPayload.SetTo("{\n");
    BString modelLine;
    modelLine.SetToFormat("  \"model\": \"%s\",\n", model);
    outPayload << modelLine;

    if (stream)
        outPayload.Append("  \"stream\": true,\n");

    // Supporto System Prompt unificato
    BString systemPrompt;
    if (config) {
        const char* sys = nullptr;
        if (config->FindString("system_prompt", &sys) == B_OK && sys != nullptr) {
            systemPrompt = sys;
        } else if (config->FindString("instructions", &sys) == B_OK && sys != nullptr) {
            systemPrompt = sys;
        }
    }
    
    if (systemPrompt.Length() > 0) {
        outPayload << "  \"system\": \"" << EscapeStringForJson(systemPrompt.String()) << "\",\n";
    }

    outPayload.Append("  \"messages\": [\n");
    bool first = true;

    BMessage messagesMsg;
    if (config && config->FindMessage("messages", &messagesMsg) == B_OK) {
        int32 i = 0;
        BMessage msgTurn;
        while (messagesMsg.FindMessage("msg", i++, &msgTurn) == B_OK
            || messagesMsg.FindMessage(BString().SetToFormat("%ld", (long)(i-1)).String(), &msgTurn) == B_OK) {
            
            const char* type = nullptr;
            msgTurn.FindString("type", &type);

            if (type && strcmp(type, "functionCall") == 0) {
                const char* name = msgTurn.FindString("name");
                const char* toolCallId = msgTurn.FindString("tool_call_id");
                if (!toolCallId || toolCallId[0] == '\0') toolCallId = "call_dummy";

                BString cleanArgs;
                BMessage argsMsg;
                if (msgTurn.FindMessage("args", &argsMsg) == B_OK) {
                    SerializeBMessageToJson(&argsMsg, cleanArgs);
                } else {
                    const char* argsStr = msgTurn.FindString("args");
                    cleanArgs = (argsStr && argsStr[0] != '\0' ? argsStr : "{}");
                }

                if (!first) outPayload.Append(",\n");
                outPayload << "    {\n"
                           << "      \"role\": \"assistant\",\n"
                           << "      \"tool_calls\": [\n"
                           << "        {\n"
                           << "          \"id\": \"" << toolCallId << "\",\n"
                           << "          \"type\": \"function\",\n"
                           << "          \"function\": {\n"
                           << "            \"name\": \"" << name << "\",\n"
                           << "            \"arguments\": \"" << EscapeStringForJson(cleanArgs.String()) << "\"\n"
                           << "          }\n"
                           << "        }\n"
                           << "      ]\n"
                           << "    }";
                first = false;
            }
            else if (type && strcmp(type, "functionResponse") == 0) {
                const char* name = msgTurn.FindString("name");
                const char* response = msgTurn.FindString("response");
                const char* toolCallId = msgTurn.FindString("tool_call_id");
                if (!toolCallId || toolCallId[0] == '\0') toolCallId = "call_dummy";

                BString formattedResponse;
                BString respStr(response ? response : "");
                respStr.Trim();
                if (respStr.StartsWith("{") && respStr.EndsWith("}")) {
                    formattedResponse = respStr;
                } else {
                    BString escapedResp = EscapeStringForJson(respStr.String());
                    formattedResponse.SetToFormat("{\"output\":\"%s\"}", escapedResp.String());
                }

                if (!first) outPayload.Append(",\n");
                outPayload << "    {\n"
                           << "      \"role\": \"tool\",\n"
                           << "      \"tool_call_id\": \"" << toolCallId << "\",\n"
                           << "      \"name\": \"" << name << "\",\n"
                           << "      \"content\": \"" << EscapeStringForJson(formattedResponse.String()) << "\"\n"
                           << "    }";
                first = false;
            }
            else {
                const char* role = nullptr;
                const char* content = nullptr;
                msgTurn.FindString("role", &role);
                if (msgTurn.FindString("content", &content) != B_OK) {
                    msgTurn.FindString("text", &content);
                }

                if (role && content && content[0] != '\0') {
                    BString roleStr(role);
                    if (roleStr == "system") continue; // Gestito in alto
                    if (roleStr == "model") roleStr = "assistant";

                    if (roleStr == "user" || roleStr == "assistant") {
                        if (!first) outPayload.Append(",\n");
                        BString escapedContent = EscapeStringForJson(content);
                        outPayload << "    {\"role\": \"" << roleStr << "\", \"content\": \"" << escapedContent << "\"}";
                        first = false;
                    }
                }
            }
        }
    }

    if (currentPrompt && currentPrompt[0] != '\0') {
        bool alreadyAppended = false;
        if (config) {
            BMessage historyCheck;
            if (config->FindMessage("messages", &historyCheck) == B_OK) {
                int32 lastIdx = 0;
                BMessage lastTurn;
                while (historyCheck.FindMessage("msg", lastIdx, &lastTurn) == B_OK
                    || historyCheck.FindMessage(BString().SetToFormat("%ld", (long)lastIdx).String(), &lastTurn) == B_OK) {
                    lastIdx++;
                }
                if (lastIdx > 0) {
                    BMessage lastItem;
                    if (historyCheck.FindMessage("msg", lastIdx - 1, &lastItem) != B_OK) {
                        historyCheck.FindMessage(BString().SetToFormat("%ld", (long)(lastIdx - 1)).String(), &lastItem);
                    }
                    const char* lRole = lastItem.FindString("role");
                    const char* lCont = lastItem.FindString("content");
                    if (!lCont) lastItem.FindString("text", &lCont);

                    if (lRole && strcmp(lRole, "user") == 0 && lCont && strcmp(lCont, currentPrompt) == 0) {
                        alreadyAppended = true;
                    }
                }
            }
        }

        if (!alreadyAppended) {
            if (!first) outPayload.Append(",\n");
            BString escapedPrompt = EscapeStringForJson(currentPrompt);
            outPayload << "    {\"role\": \"user\", \"content\": \"" << escapedPrompt << "\"}";
            first = false;
        }
    }

    if (first) {
        outPayload << "    {\"role\": \"user\", \"content\": \"Hello\"}";
    }

    outPayload.Append("\n  ]\n}");
}

class DeepSeekStreamTarget : public BDataIO {
public:
    DeepSeekStreamTarget(const char* notifyPath)
    {
        fFile.SetTo(notifyPath, B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
        fRawResponse.SetTo("");
    }
    
    const BString& RawResponse() const { return fRawResponse; }

    virtual ssize_t Write(const void* buffer, size_t size) override
    {
        if (size == 0)
            return 0;

        fRawResponse.Append((const char*)buffer, size);

        if (fFile.InitCheck() != B_OK)
            return (ssize_t)size;

        fBuffer.Append((const char*)buffer, size);
        int32 processedPos = 0;

        while (true) {
            int32 nextNewline = fBuffer.FindFirst("\n", processedPos);
            if (nextNewline == B_ERROR)
                break;

            BString line;
            fBuffer.CopyInto(line, processedPos, nextNewline - processedPos);
            line.Trim();
            processedPos = nextNewline + 1;

            if (line.IsEmpty() || line.StartsWith("event:"))
                continue;
            if (line == "data: [DONE]")
                continue;

            if (line.StartsWith("data: ")) {
                line.Remove(0, 6);
                
                int32 contentPos = line.FindFirst("\"content\":\"");
                int32 advance = 11;
                if (contentPos == B_ERROR) {
                    contentPos = line.FindFirst("\"content\": \"");
                    advance = 12;
                }

                if (contentPos != B_ERROR) {
                    contentPos += advance;
                    int32 endContent = line.FindFirst("\"", contentPos);
                    while (endContent != B_ERROR && line.ByteAt(endContent - 1) == '\\')
                        endContent = line.FindFirst("\"", endContent + 1);

                    if (endContent != B_ERROR) {
                        BString token;
                        line.CopyInto(token, contentPos, endContent - contentPos);
                        token.ReplaceAll("\\n", "\n");
                        token.ReplaceAll("\\t", "\t");
                        token.ReplaceAll("\\\"", "\"");
                        token.ReplaceAll("\\\\", "\\");
                        fFile.Write(token.String(), token.Length());
                    }
                }
            }
        }

        if (processedPos > 0)
            fBuffer.Remove(0, processedPos);

        return (ssize_t)size;
    }

private:
    BFile   fFile;
    BString fBuffer;
    BString fRawResponse;
};

extern "C" ai_plugin_t ai_plugin_init(void)
{
    AIPluginHandle* h = new(std::nothrow) AIPluginHandle();
    return (ai_plugin_t)h;
}

extern "C" void ai_plugin_free(ai_plugin_t handle)
{
    AIPluginHandle* deepseek = (AIPluginHandle*)handle;
    if (!deepseek)
        return;
    delete deepseek;
}

extern "C" uint32 ai_plugin_get_capabilities(void)
{
    return AI_CAP_STREAMING | AI_CAP_MCP;
}

extern "C" status_t ai_plugin_generate_text_sync(ai_plugin_t handle,
    const char* prompt, char* response_buf, size_t response_len,
    BMessage* contextMsg)
{
    (void)handle;
    if (!response_buf || response_len == 0)
        return B_BAD_VALUE;
    response_buf[0] = '\0';

    const char* apiKey = nullptr;
    int32 sessionID = -1;
    const char* ctxId = nullptr;
    BMessenger serverMessenger;

    if (contextMsg != NULL) {
        contextMsg->FindString("api_key", &apiKey);
        contextMsg->FindInt32("session_id", &sessionID);
        contextMsg->FindString("context_id", &ctxId);
        contextMsg->FindMessenger("server_messenger", &serverMessenger);
    }

    if (!apiKey || apiKey[0] == '\0') {
        CopyToBuffer("[deepseek_plugin] error: no API key provided", response_buf, response_len);
        DispatchError(serverMessenger, 401, sessionID, ctxId, "API key assente");
        return B_BAD_VALUE;
    }

    const char* baseUrlRaw = nullptr;
    if (contextMsg)
        contextMsg->FindString("base_url", &baseUrlRaw);
    if (!baseUrlRaw || baseUrlRaw[0] == '\0') {
        baseUrlRaw = DEFAULT_DEEPSEEK_URL;
    }

    BString url = baseUrlRaw;
    if (url.EndsWith("/")) url.Remove(url.Length() - 1, 1);
    if (!url.EndsWith("/v1/chat/completions")) url << "/v1/chat/completions";

    BString payload;
    BuildPayloadFromContext(contextMsg, prompt, payload, false);

    BMallocIO* out = new BMallocIO();
    SyncListener listener;
    BUrl bUrl(url.String(), true);
    BUrlRequest* req = BUrlProtocolRoster::MakeRequest(bUrl, out, &listener, NULL);
    if (!req) {
        delete out;
        CopyToBuffer("{\"error\":\"request failed\"}", response_buf, response_len);
        DispatchError(serverMessenger, 500, sessionID, ctxId, "Richiesta di rete fallita");
        return B_ERROR;
    }

    BHttpRequest* http = dynamic_cast<BHttpRequest*>(req);
    if (http) {
        http->SetMethod(B_HTTP_POST);
        BHttpHeaders headers;
        headers.AddHeader("Content-Type", "application/json");
        headers.AddHeader("User-Agent", DEEPSEEK_USER_AGENT);
        BString authHeader;
        authHeader.SetToFormat("Bearer %s", apiKey);
        headers.AddHeader("Authorization", authHeader.String());
        http->SetHeaders(headers);

        BMallocIO* in = new BMallocIO();
        in->WriteExactly(payload.String(), payload.Length());
        http->AdoptInputData(in, payload.Length());
    }

    thread_id thread = req->Run();
    status_t rc = B_ERROR;
    if (thread >= 0)
        wait_for_thread(thread, &rc);
    else
        rc = thread;

    int32 statusCode = 0;
    if (http) {
        const BHttpResult* httpResult = dynamic_cast<const BHttpResult*>(&(http->Result()));
        if (httpResult)
            statusCode = httpResult->StatusCode();
    }

    const void* buf = out->Buffer();
    size_t len = out->BufferLength();
    BString rawResponse;
    if (buf && len > 0)
        rawResponse.SetTo((const char*)buf, len);
    BMessage parsedJson;
    bool parseSuccess = (BJson::Parse(rawResponse.String(), parsedJson) == B_OK);

    bool extracted = false;
    if (statusCode == 200 && len > 0 && parseSuccess) {
        BMessage parsedJson;
        //if (BJson::Parse(rawResponse.String(), parsedJson) == B_OK) {
        BMessage choices, choiceZero, message;
        const char* content = nullptr;
        bool found = (parsedJson.FindMessage("choices", &choices) == B_OK)
        	&& (choices.FindMessage("0", &choiceZero) == B_OK)
            && (choiceZero.FindMessage("message", &message) == B_OK)
            && (message.FindString("content", &content) == B_OK && content != nullptr);
            
        if (found){
          	CopyToBuffer(content, response_buf, response_len);
            extracted = true;
        }
        if (!contextMsg->HasString("title") && prompt != nullptr) {
                BString autoTitle(prompt);
                autoTitle.Trim();
                if (autoTitle.Length() > 30) {
                    autoTitle.Truncate(30);
                    autoTitle << "...";
                }
                contextMsg->RemoveName("title");
                contextMsg->AddString("title", autoTitle.String());
        }
    } else if (statusCode != 200 && parseSuccess) {
        BMessage errorObj;
        if (parsedJson.FindMessage("error", &errorObj) == B_OK) {
            const char* errMsg = errorObj.FindString("message");
            if (errMsg) {
                snprintf(response_buf, response_len, "[Errore OpenAI %" B_PRId32 "] %s", statusCode, errMsg);
            } else {
                snprintf(response_buf, response_len, "[Errore OpenAI %" B_PRId32 "]", statusCode);
            }
            extracted = true;
        }
    }

    if (!extracted) {
        if (len > 0)
            CopyToBuffer(rawResponse.String(), response_buf, response_len);
        else
            CopyToBuffer("{\"error\":\"empty response\"}", response_buf, response_len);
    }

    delete req;
    delete out;
    return B_OK;//(rc == B_OK && statusCode == 200) ? B_OK : B_ERROR;
}

static int32 deepseek_stream_thread_func(void* data)
{
    AsyncArgs* args = (AsyncArgs*)data;
    if (!args) return B_BAD_VALUE;

    BMessage replyTools;
    BString baseUrl;
    BString url;
    bool mcpActive = false;
    bool executionLoop = true;
    bool hasError = false;

    const char* ctxId = nullptr;
    int32 sessionID = -1;

    if (args->context_copy != nullptr) {
        args->context_copy->FindString("context_id", &ctxId);
        args->context_copy->FindInt32("session_id", &sessionID);
    }

    if (!args->api_key || args->api_key[0] == '\0') {
        DispatchError(args->server_messenger, 401, sessionID, ctxId, "API key assente");
        if (args->notify_path) {
            BFile streamFile(args->notify_path, B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
            BString endMarker = "<<STREAM_END>>";
            streamFile.Write(endMarker.String(), endMarker.Length());
        }
        goto thread_cleanup;
    }

    baseUrl = (args->base_url && args->base_url[0] != '\0') ? args->base_url : DEFAULT_DEEPSEEK_URL;
    url = baseUrl;
    if (url.EndsWith("/")) url.Remove(url.Length() - 1, 1);
    if (!url.EndsWith("/v1/chat/completions")) url << "/v1/chat/completions";

    if (args->server_messenger.IsValid()) {
        BMessage reqTools(MSG_MCP_GET_TOOLS); 
        if (ctxId) reqTools.AddString("context_id", ctxId);
        
        if (args->server_messenger.SendMessage(&reqTools, &replyTools) == B_OK) {
            if (replyTools.HasMessage("tool", 0)) {
                mcpActive = true;
            }
        }
    }

    if (!mcpActive) {
        goto fallback_to_standard;
    }

    executionLoop = true;
    while (executionLoop) {
        if (args->server_messenger.IsValid()) {
            BMessage checkAbortMsg('CHAB');
            if (ctxId) checkAbortMsg.AddString("context_id", ctxId);
            
            BMessage abortReply;
            if (args->server_messenger.SendMessage(&checkAbortMsg, &abortReply) == B_OK) {
                int32 status = B_OK;
                if (abortReply.FindInt32("status", &status) == B_OK && status == B_CANCELED) {
                    executionLoop = false;
                    if (args->notify_path != NULL) {
                        BFile streamFile(args->notify_path, B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
                        if (streamFile.InitCheck() == B_OK) streamFile.Write("<<STREAM_ABORT>>", 16);
                    }
                    break;
                }
            }
        }

        BString payload;
        BuildPayloadFromContext(args->context_copy, nullptr, payload, false);

        if (mcpActive) {
            BString openAiToolsJson;
            ConvertBMessageToOpenAIToolsJson(&replyTools, openAiToolsJson);

            if (openAiToolsJson.Length() > 0) {
                int32 lastCloseBrace = payload.FindLast("}");
                if (lastCloseBrace != B_ERROR) {
                    payload.Truncate(lastCloseBrace);
                    payload << ",\"tools\":" << openAiToolsJson << "}";
                }
            }
        }

        BMallocIO outNetworkData;
        SyncListener syncListener;
        BUrl bUrl(url.String(), true);
        BUrlRequest* req = BUrlProtocolRoster::MakeRequest(bUrl, &outNetworkData, &syncListener, NULL);
        if (!req) { 
            hasError = true;
            executionLoop = false; 
            break; 
        }

        uint32 httpStatusCode = 0;
        BHttpRequest* http = dynamic_cast<BHttpRequest*>(req);
        if (http) {
            http->SetMethod(B_HTTP_POST);
            BHttpHeaders headers;
            headers.AddHeader("Content-Type", "application/json");
            headers.AddHeader("User-Agent", DEEPSEEK_USER_AGENT);
            BString authHeader;
            authHeader << "Bearer " << args->api_key;
            headers.AddHeader("Authorization", authHeader.String());
            http->SetHeaders(headers);
            
            BMallocIO* input = new BMallocIO();
            input->WriteExactly(payload.String(), payload.Length());
            http->AdoptInputData(input, payload.Length());
        }

        thread_id netThread = req->Run();
        if (netThread >= 0) { 
            status_t rc; 
            wait_for_thread(netThread, &rc); 
        }

        if (http) {
            const BHttpResult* result = dynamic_cast<const BHttpResult*>(&http->Result());
            if (result != nullptr) httpStatusCode = result->StatusCode();
        }
        delete req;

        BString rawResponse((const char*)outNetworkData.Buffer(), outNetworkData.BufferLength());
        BMessage parsedJson;
        
        if (httpStatusCode != 200 || BJson::Parse(rawResponse.String(), parsedJson) != B_OK) {
            DispatchError(args->server_messenger, httpStatusCode, sessionID, ctxId, rawResponse);
            hasError = true;
            executionLoop = false;
            break;
        }
        
        BMessage errorObj;
        if (parsedJson.FindMessage("error", &errorObj) == B_OK) {
            const char* errMsg = nullptr;
            errorObj.FindString("message", &errMsg);
            if (errMsg && (BString(errMsg).FindFirst("tool choice") != B_ERROR || 
                           BString(errMsg).FindFirst("tools") != B_ERROR)) {
                mcpActive = false; 
                executionLoop = false;
                goto fallback_to_standard;
            }
            DispatchError(args->server_messenger, httpStatusCode, sessionID, ctxId, rawResponse);
            hasError = true;
            executionLoop = false;
            break;
        }

        BMessage choices, choiceZero, message, toolCalls, toolCallZero, functionObj;
        const char* toolName = nullptr;
        const char* toolCallId = nullptr;
        const char* argumentsStr = nullptr;
        const char* textContent = nullptr;

        if (parsedJson.FindMessage("choices", &choices) == B_OK
            && choices.FindMessage("0", &choiceZero) == B_OK
            && choiceZero.FindMessage("message", &message) == B_OK) {

            if (message.FindMessage("tool_calls", &toolCalls) == B_OK
                && toolCalls.FindMessage("0", &toolCallZero) == B_OK
                && toolCallZero.FindMessage("function", &functionObj) == B_OK
                && functionObj.FindString("name", &toolName) == B_OK) {
                
                toolCallZero.FindString("id", &toolCallId);
                functionObj.FindString("arguments", &argumentsStr);

                BMessage argsMsg;
                if (argumentsStr && BJson::Parse(argumentsStr, argsMsg) != B_OK) {
                    // Fallback se gli argomenti non sono un JSON valido
                }

                BMessage reqExec(MSG_EXECUTE_TOOL);
                reqExec.AddString("name", toolName);
                reqExec.AddMessage("arguments", &argsMsg);
                if (ctxId) reqExec.AddString("context_id", ctxId);
                
                BMessage replyExec;
                BString toolResultBuf;
                
                if (args->server_messenger.SendMessage(&reqExec, &replyExec) == B_OK) {
                    const char* resStr = replyExec.FindString("result");
                    if (resStr && strlen(resStr) > 0) {
                        BString testStr(resStr);
                        testStr.Trim();
                        if (testStr.StartsWith("{") || testStr.StartsWith("[")) {
                            toolResultBuf = resStr;
                        } else {
                            BString escapedRes = EscapeStringForJson(resStr);
                            toolResultBuf.SetToFormat("{\"output\":\"%s\"}", escapedRes.String());
                        }
                    } else {
                        toolResultBuf = "{\"error\":\"Il comando sul server ha restituito una risposta vuota.\"}";
                    }
                } else {
                    toolResultBuf = "{\"error\":\"Esecuzione dello strumento fallita via IPC BMessenger\"}";
                }

                AppendToolCallToContext(args->context_copy, toolName, &argsMsg, toolCallId);
                AppendToolResponseToContext(args->context_copy, toolName, toolResultBuf.String(), toolCallId);
            } 
            else if (message.FindString("content", &textContent) == B_OK && textContent != nullptr) {
                BFile streamFile(args->notify_path, B_WRITE_ONLY | B_CREATE_FILE | B_OPEN_AT_END);
                if (streamFile.InitCheck() == B_OK) {
                    streamFile.Write(textContent, strlen(textContent));
                }
                executionLoop = false;
            }
        } else {
            DispatchError(args->server_messenger, httpStatusCode, sessionID, ctxId, rawResponse);
            hasError = true;
            executionLoop = false;
        }
    }
    goto thread_post_actions;

fallback_to_standard:
{
    BString payload;
    BuildPayloadFromContext(args->context_copy, nullptr, payload, true);

    DeepSeekStreamTarget streamTarget(args->notify_path);
    CompletionListener listener(args->notify_path);
    BUrl bUrl(url.String(), true);
    BUrlRequest* req = BUrlProtocolRoster::MakeRequest(bUrl, &streamTarget, &listener, NULL);
    if (req) {
        BHttpRequest* http = dynamic_cast<BHttpRequest*>(req);
        if (http) {
            http->SetMethod(B_HTTP_POST);
            BHttpHeaders headers;
            headers.AddHeader("Content-Type", "application/json");
            headers.AddHeader("User-Agent", DEEPSEEK_USER_AGENT);
            BString authHeader;
            authHeader << "Bearer " << args->api_key;
            headers.AddHeader("Authorization", authHeader.String());
            http->SetHeaders(headers);

            BMallocIO* input = new BMallocIO();
            input->WriteExactly(payload.String(), payload.Length());
            http->AdoptInputData(input, payload.Length());
        }
        
        thread_id thread = req->Run();
        if (thread >= 0) { 
            status_t rc; 
            wait_for_thread(thread, &rc); 
        }

        if (http != NULL) {
            const BHttpResult& httpResult = static_cast<const BHttpResult&>(http->Result());
            int32 statusCode = httpResult.StatusCode();
            if (statusCode != 200) {
                BString errorBody = streamTarget.RawResponse();
                DispatchError(args->server_messenger, statusCode, sessionID, ctxId, errorBody);

                BFile streamFile(args->notify_path, B_WRITE_ONLY | B_CREATE_FILE | B_OPEN_AT_END);
                if (streamFile.InitCheck() == B_OK) {
                    BString errBuffer;
                    if (errorBody.Length() > 0) {
                        errBuffer.SetToFormat("\n[ERRORE API %" B_PRId32 "]: %s\n", statusCode, errorBody.String());
                    } else {
                        errBuffer.SetToFormat("\n[ERRORE API %" B_PRId32 ": Verificare configurazione o parametri payload]\n", statusCode);
                    }
                    streamFile.Write(errBuffer.String(), errBuffer.Length());
                }
                hasError = true;
            }
        }
        delete req;
    } else {
        hasError = true;
    }
}

thread_post_actions:
    if (!hasError && args->context_copy != nullptr && !args->context_copy->HasString("title")) {
        BMessage messagesMsg;
        const char* firstPromptText = nullptr;

        if (args->context_copy->FindMessage("messages", &messagesMsg) == B_OK) {
            int32 msgCount = 0;
            BMessage msgItem;
            
            while (messagesMsg.FindMessage("msg", msgCount, &msgItem) == B_OK || 
                   messagesMsg.FindMessage(BString().SetToFormat("%d", msgCount).String(), &msgItem) == B_OK) {
                
                const char* role = msgItem.FindString("role");
                const char* content = msgItem.FindString("content");
                if (!content) msgItem.FindString("text", &content);
                
                if (role && strcmp(role, "user") == 0 && content && content[0] != '\0') {
                    firstPromptText = content;
                    break;
                }
                msgCount++;
            }
        }

        if (firstPromptText && firstPromptText[0] != '\0') {
            BString autoTitle(firstPromptText);
            autoTitle.Trim();
            if (autoTitle.Length() > 30) {
                autoTitle.Truncate(30);
                autoTitle << "...";
            }
            
            args->context_copy->RemoveName("title");
            args->context_copy->AddString("title", autoTitle.String());
            
            if (args->server_messenger.IsValid()) {
                BMessage titleUpdateMsg('UTIT');
                titleUpdateMsg.AddString("title", autoTitle.String());
                args->server_messenger.SendMessage(&titleUpdateMsg);
            }
        }
    }

    {
        BFile streamFile(args->notify_path, B_WRITE_ONLY | B_CREATE_FILE | B_OPEN_AT_END);
        if (streamFile.InitCheck() == B_OK) {
            BString endMarker = "<<STREAM_END>>";
            streamFile.Write(endMarker.String(), endMarker.Length());
        }
    }

thread_cleanup:
    delete args; 
    return B_OK;
}

extern "C" status_t ai_plugin_generate_text_async(ai_plugin_t handle,
    const char* prompt, BMessage* contextMsg)
{
    (void)handle;
    if (!contextMsg)
        return B_BAD_VALUE;

    const char* apiKey = nullptr;
    const char* model = nullptr;
    const char* notifyPath = nullptr;
    const char* configBaseUrl = nullptr;
    
    contextMsg->FindString("api_key", &apiKey);
    contextMsg->FindString("model_name", &model);
    contextMsg->FindString("notify_path", &notifyPath);
    contextMsg->FindString("base_url", &configBaseUrl);

    if (!notifyPath || notifyPath[0] == '\0')
        return B_BAD_VALUE;

    AsyncArgs* args = new (std::nothrow) AsyncArgs();
    if (!args)
        return B_NO_MEMORY;

    args->api_key = dupstr_or_null(apiKey);
    args->model = dupstr_or_null((model && model[0] != '\0') ? model : DEFAULT_DEEPSEEK_MODEL);
    args->notify_path = dupstr_or_null(notifyPath);
    
    if (configBaseUrl && configBaseUrl[0] != '\0') {
        args->base_url = dupstr_or_null(configBaseUrl);
    } else {
        args->base_url = strdup(DEFAULT_DEEPSEEK_URL);
    }

    args->context_copy = new (std::nothrow) BMessage(*contextMsg);
    if (!args->context_copy) {
        delete args;
        return B_NO_MEMORY;
    }

    BMessenger serverMessenger;
    if (contextMsg->FindMessenger("server_messenger", &serverMessenger) == B_OK) {
        args->server_messenger = serverMessenger;
    } else {
        args->server_messenger = BMessenger();
    }

    // Inserimento prompt iniziale se mancante nello storico
    if (prompt && prompt[0] != '\0') {
        BMessage messagesMsg;
        BMessage firstTurn;
        bool hasHistory = args->context_copy->FindMessage("messages", &messagesMsg) == B_OK
            && (messagesMsg.FindMessage("msg", 0, &firstTurn) == B_OK || messagesMsg.FindMessage("0", &firstTurn) == B_OK);

        if (!hasHistory) {
            BMessage newMessages;
            BMessage turn;
            turn.AddString("role", "user");
            turn.AddString("content", prompt);
            newMessages.AddMessage("msg", &turn);

            if (args->context_copy->ReplaceMessage("messages", &newMessages) != B_OK)
                args->context_copy->AddMessage("messages", &newMessages);
        }
    }

    thread_id thread = spawn_thread(deepseek_stream_thread_func,
        "deepseek_stream_worker", B_NORMAL_PRIORITY, args);
    if (thread < B_OK) {
        delete args;
        return B_ERROR;
    }

    resume_thread(thread);
    return B_OK;
}

extern "C" status_t ai_plugin_list_models(const BMessage* settingsMsg,
    char* out_buf, size_t out_len)
{
    (void)settingsMsg;
    const char* modelsJson = "[\"deepseek-chat\",\"deepseek-reasoner\"]";
    return CopyToBuffer(modelsJson, out_buf, out_len);
}

extern "C" status_t ai_plugin_set_model(ai_plugin_t handle, const char* model_name)
{
    if (!handle || !model_name || model_name[0] == '\0')
        return B_BAD_VALUE;
    return B_OK;
}

extern "C" const char* get_plugin_name()
{
    return "DeepSeekPlugin";
}
