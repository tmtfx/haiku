// publicai_plugin.cpp
// Plugin per il servizio Public AI su Haiku - Versione Stateless Concorrente Nativa.

#include <os/ai/AIPlugin.h>
#include <os/ai/AINetworkPlugin.h>
#include <os/ai/AICommands.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <new>

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

#define DEFAULT_PUBLICAI_BASE_URL "https://api.publicai.co"
#define DEFAULT_PUBLICAI_MODEL    "swiss-ai/apertus-8b-instruct"

struct PublicAIHandle : public AIPluginHandle {
    PublicAIHandle() = default;
    virtual ~PublicAIHandle() = default;
};

// --- Serializzazione JSON e gestione BMessage ---

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
            case '\n': escaped << "\\n"; break;
            case '\r': escaped << "\\r"; break;
            case '\t': escaped << "\\t"; break;
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

static void SerializeBMessageToJson(const BMessage* msg, BString& outJson) {
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

static char* dupstr_or_null(const char* s) {
    if (!s) return nullptr;
    size_t n = strlen(s) + 1;
    char* p = (char*)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

static void BuildPublicAIPayload(const BMessage* chatContext, 
                                 const char* explicitPrompt, 
                                 BString& outPayload, 
                                 bool stream = false,
                                 const BString* toolsJson = nullptr)
{
    const char* model = nullptr;
    if (chatContext) chatContext->FindString("model_name", &model);
    if (!model || model[0] == '\0') model = DEFAULT_PUBLICAI_MODEL;

    outPayload.SetTo("{\n");
    BString modelLine;
    modelLine.SetToFormat("  \"model\": \"%s\",\n", model);
    outPayload << modelLine;

    if (stream) {
        outPayload.Append("  \"stream\": true,\n");
    }

    outPayload.Append("  \"messages\": [\n");

    bool first = true;
    const char* systemPrompt = nullptr;
    if (chatContext) {
        chatContext->FindString("system_prompt", &systemPrompt);
    }
    if (systemPrompt && systemPrompt[0] != '\0') {
        BString escapedSystem = EscapeStringForJson(systemPrompt);
        outPayload << "    {\"role\": \"system\", \"content\": \"" << escapedSystem << "\"}";
        first = false;
    }

    BMessage historyMsg;
    int32 msgIndex = 0;
    BMessage lastMsgTurn;
    bool hasHistory = false;

    if (chatContext && chatContext->FindMessage("messages", &historyMsg) == B_OK) {
        hasHistory = true;
        BMessage msgTurn;
        while (historyMsg.FindMessage("msg", msgIndex++, &msgTurn) == B_OK) {
            lastMsgTurn = msgTurn;
            const char* type = nullptr;
            msgTurn.FindString("type", &type);

            if (type && strcmp(type, "functionCall") == 0) {
            	fprintf(stderr,"Not able to use functionCall");
            	/*
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
                first = false;*/
            } else if (type && strcmp(type, "functionResponse") == 0) {
				fprintf(stderr,"Not able to use functionResponse");
            	/*
                const char* response = msgTurn.FindString("response");
                const char* toolCallId = msgTurn.FindString("tool_call_id");
                if (!toolCallId || toolCallId[0] == '\0') toolCallId = "call_dummy";

                BString respStr(response ? response : "");
                respStr.Trim();

                if (!first) outPayload.Append(",\n");
                outPayload << "    {\n"
                           << "      \"role\": \"tool\",\n"
                           << "      \"tool_call_id\": \"" << toolCallId << "\",\n"
                           << "      \"content\": \"" << EscapeStringForJson(respStr.String()) << "\"\n"
                           << "    }";
                first = false;
                */
            } else {
                const char* role = nullptr;
                const char* content = nullptr;
                msgTurn.FindString("role", &role);
                if (msgTurn.FindString("content", &content) != B_OK) {
                    msgTurn.FindString("text", &content);
                }

                if (role && content) {
                    if (!first) outPayload.Append(",\n");
                    BString escapedContent = EscapeStringForJson(content);
                    outPayload << "    {\"role\": \"" << role << "\", \"content\": \"" << escapedContent << "\"}";
                    first = false;
                }
            }
        }
    }

    if (explicitPrompt && explicitPrompt[0] != '\0') {
        bool alreadyAppended = false;

        if (hasHistory && msgIndex > 1) {
            const char* lastRole = nullptr;
            const char* lastContent = nullptr;
            lastMsgTurn.FindString("role", &lastRole);
            if (lastMsgTurn.FindString("content", &lastContent) != B_OK) {
                lastMsgTurn.FindString("text", &lastContent);
            }

            if (lastRole && strcmp(lastRole, "user") == 0 &&
                lastContent && strcmp(lastContent, explicitPrompt) == 0) {
                alreadyAppended = true;
            }
        }

        if (!alreadyAppended) {
            if (!first) outPayload.Append(",\n");
            BString escapedPrompt = EscapeStringForJson(explicitPrompt);
            outPayload << "    {\"role\": \"user\", \"content\": \"" << escapedPrompt << "\"}";
        }
    }

    outPayload.Append("\n  ]");
/*
    if (toolsJson && toolsJson->Length() > 0) {
        outPayload.Append(",\n  \"tools\": ");
        outPayload.Append(*toolsJson);
    }*/

    outPayload.Append("\n}");
}

class StreamTarget : public BDataIO {
public:
    StreamTarget(const char* notifyPath) {
        fFile.SetTo(notifyPath, B_WRITE_ONLY | B_CREATE_FILE | B_OPEN_AT_END);
        fBuffer.SetTo("");
        fRawResponse.SetTo("");
    }

    virtual ssize_t Write(const void* buffer, size_t size) override {
        if (size == 0) return 0;

        fRawResponse.Append((const char*)buffer, size);
        
        if (fFile.InitCheck() != B_OK) return size;

        fBuffer.Append((const char*)buffer, size);

        int32 lineStart = 0;
        int32 lineEnd = 0;

        while ((lineEnd = fBuffer.FindFirst("\n", lineStart)) != B_ERROR) {
            BString line;
            fBuffer.CopyInto(line, lineStart, lineEnd - lineStart);
            lineStart = lineEnd + 1;

            line.Trim();
            if (line.IsEmpty() || !line.StartsWith("data:"))
                continue;

            line.Remove(0, 5);
            line.Trim();

            if (line == "[DONE]")
                break;

            BMessage parsedJson;
            if (BJson::Parse(line.String(), parsedJson) == B_OK) {
                BMessage choices, choiceZero, delta;
                const char* contentText = nullptr;

                if (parsedJson.FindMessage("choices", &choices) == B_OK) {
                    if (choices.FindMessage("0", &choiceZero) == B_OK || 
                        choices.FindMessage("msg", 0, &choiceZero) == B_OK) {
                        
                        if (choiceZero.FindMessage("delta", &delta) == B_OK) {
                            delta.FindString("content", &contentText);
                        }
                    }
                }

                if (contentText != nullptr && contentText[0] != '\0') {
                    fFile.Write(contentText, strlen(contentText));
                    fFile.Flush();
                }
            }
        }

        if (lineStart > 0) {
            fBuffer.Remove(0, lineStart);
        }

        return size;
    }
    
    const BString& RawResponse() const {
        return fRawResponse;
    }

private:
    BFile fFile;
    BString fBuffer;
    BString fRawResponse;
};

// --- INTERFACCIA C PUBBLICA PER HAIKU AI KIT ---

extern "C" ai_plugin_t ai_plugin_init()
{
    PublicAIHandle* h = new(std::nothrow) PublicAIHandle();
    if (!h) return nullptr;
    return (ai_plugin_t)h;
}

extern "C" void ai_plugin_free(ai_plugin_t handle)
{
    AIPluginHandle* h = static_cast<AIPluginHandle*>(handle);
    delete h;
}

extern "C" status_t
ai_plugin_generate_text_sync(ai_plugin_t handle,
                             const char* prompt,
                             char* response_buf,
                             size_t response_len,
                             BMessage* config)
{
    if (!config || !response_buf || response_len == 0)
        return B_ERROR;

    const char* apiKeyRaw = nullptr;
    config->FindString("api_key", &apiKeyRaw);
    if (!apiKeyRaw || apiKeyRaw[0] == '\0') {
        snprintf(response_buf, response_len, "[publicai_plugin] error: no API key provided");
        return B_ERROR;
    }
    BString apiKey(apiKeyRaw);

    BString baseUrl;
    const char* baseUrlRaw = nullptr;
    if (config->FindString("base_url", &baseUrlRaw) == B_OK && baseUrlRaw != nullptr && baseUrlRaw[0] != '\0') {
        baseUrl = baseUrlRaw;
    } else {
        baseUrl = DEFAULT_PUBLICAI_BASE_URL;
    }

    const char* ctxId = nullptr;
    config->FindString("context_id", &ctxId);

    int32 sessionID = -1;
    config->FindInt32("session_id", &sessionID);

    BMessenger serverMessenger;
    config->FindMessenger("server_messenger", &serverMessenger);

    BString targetUrl = baseUrl;
    if (targetUrl.EndsWith("/"))
        targetUrl.Remove(targetUrl.Length() - 1, 1);
    if (!targetUrl.EndsWith("/v1/chat/completions"))
        targetUrl << "/v1/chat/completions";

    BString payload;
    BuildPublicAIPayload(config, prompt, payload, false, nullptr);

    BMallocIO* out = new BMallocIO();
    SyncListener syncListener;
    BUrl bUrl(targetUrl.String(), true);
    BUrlContext context;
    
    BUrlRequest* req = BUrlProtocolRoster::MakeRequest(bUrl, out, &syncListener, &context);
    if (!req) {
        delete out;
        snprintf(response_buf, response_len, "{\"error\":\"request creation failed\"}");
        return B_ERROR;
    }

    BHttpRequest* http = dynamic_cast<BHttpRequest*>(req);
    if (http) {
        http->SetMethod(B_HTTP_POST);
        BMallocIO* in = new BMallocIO();
        in->WriteExactly(payload.String(), payload.Length());
        http->AdoptInputData(in, payload.Length());
        
        BHttpHeaders headers;
        headers.AddHeader("Content-Type", "application/json");
        BString authHeader;
        authHeader << "Bearer " << apiKey;
        headers.AddHeader("Authorization", authHeader.String());
        http->SetHeaders(headers);
    }

    thread_id thread = req->Run();
    status_t rc = B_ERROR;
    if (thread >= 0) {
        wait_for_thread(thread, &rc);
    } else {
        rc = thread;
    }

    const void* buf = out->Buffer();
    size_t len = out->BufferLength();
    if (!buf || len == 0) {
        snprintf(response_buf, response_len, "{\"error\":\"empty response\"}");
        delete req;
        delete out;
        return B_ERROR;
    }

    int32 statusCode = 0;
    if (http) {
        const BHttpResult* httpResult = dynamic_cast<const BHttpResult*>(&(http->Result()));
        if (httpResult != nullptr) {
            statusCode = httpResult->StatusCode();
        }
    }

    bool handled = false;
    BString rawResponse((const char*)buf, len);
    BMessage parsedJson;
    bool parseSuccess = (BJson::Parse(rawResponse.String(), parsedJson) == B_OK);

    if (statusCode == 200 && parseSuccess) {
        BMessage choices, choiceZero, message;
        const char* extractedText = nullptr;

        bool found = (parsedJson.FindMessage("choices", &choices) == B_OK)
            && (choices.FindMessage("0", &choiceZero) == B_OK || choices.FindMessage("msg", 0, &choiceZero) == B_OK)
            && (choiceZero.FindMessage("message", &message) == B_OK)
            && (message.FindString("content", &extractedText) == B_OK && extractedText != nullptr);

        if (found) {
            size_t textLen = strlen(extractedText);
            size_t copy_len = textLen < response_len - 1 ? textLen : response_len - 1;
            memcpy(response_buf, extractedText, copy_len);
            response_buf[copy_len] = '\0';
            handled = true;

            if (!config->HasString("title") && prompt != nullptr) {
                BString autoTitle(prompt);
                autoTitle.Trim();
                if (autoTitle.Length() > 30) {
                    autoTitle.Truncate(30);
                    autoTitle << "...";
                }
                config->RemoveName("title");
                config->AddString("title", autoTitle.String());
            }
        }
    } else if (statusCode != 200 && parseSuccess) {
        BMessage errorObj;
        if (parsedJson.FindMessage("error", &errorObj) == B_OK) {
            const char* errMsg = errorObj.FindString("message");
            if (errMsg) {
                snprintf(response_buf, response_len, "[Errore PublicAI %" B_PRId32 "] %s", statusCode, errMsg);
            } else {
                snprintf(response_buf, response_len, "[Errore PublicAI %" B_PRId32 "]", statusCode);
            }
            handled = true;
        }
    }

    if (!handled) {
        if (len > 0) {
            size_t copy_len = len < response_len - 1 ? len : response_len - 1;
            memcpy(response_buf, buf, copy_len);
            response_buf[copy_len] = '\0';
        } else {
            snprintf(response_buf, response_len, "[Errore PublicAI] Risposta vuota o non valida (HTTP status: %" B_PRId32 ")", statusCode);
        }
    }

    delete req;
    delete out;
    return B_OK;
}

static status_t
publicai_stream_thread_func(void* data)
{
    BString baseUrl;
    BString targetUrl;
    BString tempUrl;
    
    AsyncArgs* args = (AsyncArgs*)data;
    if (!args) return B_BAD_VALUE;
    
    const char* ctxId = nullptr;
    int32 sessionID = -1;
    
    if (args->context_copy) {
        args->context_copy->FindString("context_id", &ctxId);
        args->context_copy->FindInt32("session_id", &sessionID);
    }
    
    if (args->base_url && args->base_url[0] != '\0') {
        baseUrl = args->base_url;
    } else if (args->context_copy && args->context_copy->FindString("base_url", &tempUrl) == B_OK 
               && !tempUrl.IsEmpty()) {
        baseUrl = tempUrl;
    } else {
        baseUrl = DEFAULT_PUBLICAI_BASE_URL;
    }
    
    if (baseUrl.Length() > 0) {
        targetUrl = baseUrl;
        if (targetUrl.EndsWith("/")) 
            targetUrl.Remove(targetUrl.Length() - 1, 1);
        if (!targetUrl.EndsWith("/v1/chat/completions")) 
            targetUrl << "/v1/chat/completions";
    } else {
        targetUrl = "https://api.publicai.co/v1/chat/completions";
    }

    BString payload;
    BuildPublicAIPayload(args->context_copy, nullptr, payload, true, nullptr);

    {
        StreamTarget streamTarget(args->notify_path);
        CompletionListener listener(args->notify_path);
        BUrl bUrl(targetUrl.String(), true);
        BUrlRequest* req = BUrlProtocolRoster::MakeRequest(bUrl, &streamTarget, &listener, NULL);
        if (req) {
            BHttpRequest* http = dynamic_cast<BHttpRequest*>(req);
            if (http) {
                http->SetMethod(B_HTTP_POST);
                BHttpHeaders headers;
                headers.AddHeader("Content-Type", "application/json");
                BString authHeader;
                authHeader << "Bearer " << args->api_key;
                headers.AddHeader("Authorization", authHeader.String());
                http->SetHeaders(headers);

                BMemoryIO* input = new BMemoryIO(payload.String(), payload.Length());
                http->AdoptInputData(input, payload.Length());
            }
            thread_id thread = req->Run();
            if (thread >= 0) { 
                status_t rc; 
                wait_for_thread(thread, &rc); 
            }

            if (http) {
                const BHttpResult* result = dynamic_cast<const BHttpResult*>(&http->Result());
                if (result != nullptr && result->StatusCode() != 200) {
                    DispatchError(args->server_messenger, result->StatusCode(), sessionID, ctxId, streamTarget.RawResponse());
                }
            }

            delete req;
        }
    }

    if (args->context_copy != nullptr && !args->context_copy->HasString("title")) {
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

    delete args; 
    return B_OK;
}

extern "C" status_t ai_plugin_generate_text_async(ai_plugin_t handle, const char* prompt, BMessage* config)
{
    if (!config) return B_ERROR;
    
    const char* apiKey = nullptr;
    const char* modelName = nullptr;
    const char* notifyPath = nullptr;
    const char* configBaseUrl = nullptr;
    
    config->FindString("api_key", &apiKey);
    config->FindString("model_name", &modelName);
    config->FindString("notify_path", &notifyPath);
    config->FindString("base_url", &configBaseUrl);

    if (!notifyPath || notifyPath[0] == '\0') {
        return B_ERROR;
    }
    
    AsyncArgs* args = new (std::nothrow) AsyncArgs();
    if (!args) return B_ERROR;
    
    args->api_key = dupstr_or_null(apiKey);
    args->model = dupstr_or_null(modelName && modelName[0] ? modelName : DEFAULT_PUBLICAI_MODEL);
    args->notify_path = dupstr_or_null(notifyPath);

    if (configBaseUrl && configBaseUrl[0] != '\0') {
        args->base_url = dupstr_or_null(configBaseUrl);
    } else {
        args->base_url = strdup(DEFAULT_PUBLICAI_BASE_URL);
    }

    args->context_copy = new (std::nothrow) BMessage(*config);
    if (!args->context_copy) {
        delete args;
        return B_NO_MEMORY;
    }
       
    int32 sessionID = -1;
    args->context_copy->FindInt32("session_id", &sessionID);

    if (prompt && prompt[0] != '\0' && !args->context_copy->HasString("prompt")) {
        args->context_copy->AddString("prompt", prompt);
    }

    BMessenger serverMessenger;
    if (config->FindMessenger("server_messenger", &serverMessenger) == B_OK) {
        args->server_messenger = serverMessenger;
    } else {
        args->server_messenger = BMessenger();
    }

    thread_id thread = spawn_thread(
        publicai_stream_thread_func,
        "publicai_stream_worker",
        B_NORMAL_PRIORITY,
        args
    );

    if (thread < B_OK) {
        delete args;
        return B_ERROR;
    }

    resume_thread(thread);
    return B_OK;
}

extern "C" status_t ai_plugin_list_models(const BMessage* config, char* out_buf, size_t out_len)
{
    const char* defaultFallback = "[\"swiss-ai/apertus-8b-instruct\"]";

    if (!out_buf || out_len == 0) return B_BAD_VALUE;

    if (!config) {
        if (out_len > strlen(defaultFallback)) {
            strcpy(out_buf, defaultFallback);
            return B_OK;
        }
        return B_ERROR;
    }

    BMessenger serverMessenger;
    config->FindMessenger("server_messenger", &serverMessenger);

    int32 sessionID = -1;
    config->FindInt32("session_id", &sessionID);

    const char* ctxId = nullptr;
    config->FindString("context_id", &ctxId);

    const char* apiKey = nullptr;
    config->FindString("api_key", &apiKey);
    
    BString baseUrl;
    if (config->FindString("base_url", &baseUrl) != B_OK || baseUrl.IsEmpty()) {
        baseUrl = DEFAULT_PUBLICAI_BASE_URL;
    }

    BString url = baseUrl;
    if (url.EndsWith("/")) {
        url.Remove(url.Length() - 1, 1);
    }

    if (!url.EndsWith("/v1") && !url.EndsWith("/models")) {
        url << "/v1/models";
    } else if (!url.EndsWith("/models")) {
        url << "/models";
    }

    BMallocIO* out = new (std::nothrow) BMallocIO();
    if (!out) return B_NO_MEMORY;

    SyncListener listener;
    BUrl bUrl(url.String(), true);

    BUrlRequest* req = BUrlProtocolRoster::MakeRequest(bUrl, out, &listener, NULL);
    if (!req) { 
        delete out; 
        return B_ERROR; 
    }
    
    int32 statusCode = 0;
    BHttpRequest* http = dynamic_cast<BHttpRequest*>(req);
    if (http) {
        http->SetMethod(B_HTTP_GET);
        BHttpHeaders headers;
        headers.AddHeader("Content-Type", "application/json");
        
        if (apiKey && apiKey[0] != '\0') {
            BString authHeader;
            authHeader << "Bearer " << apiKey;
            headers.AddHeader("Authorization", authHeader.String());
        }
        http->SetHeaders(headers);
    }

    thread_id thread = req->Run();
    status_t rc = B_ERROR;
    if (thread >= 0) {
        wait_for_thread(thread, &rc);
    } else {
        rc = thread;
    }
    
    if (rc == B_OK && http != nullptr) {
        const BHttpResult* httpResult = dynamic_cast<const BHttpResult*>(&(http->Result()));
        if (httpResult != nullptr) {
            statusCode = httpResult->StatusCode();
        }
    }

    if (rc != B_OK || out->BufferLength() == 0 || statusCode != 200) {
        if (serverMessenger.IsValid()) {
            BMessage errorReport(MSG_MOD_ERROR);
            errorReport.AddString("plugin_name", "PublicAI");
            errorReport.AddInt32("http_code", statusCode);
            
            if (sessionID != -1) {
                errorReport.AddInt32("session_id", sessionID);
            }
            if (ctxId) {
                errorReport.AddString("session_id", ctxId);
            }

            BString rawError((const char*)out->Buffer(), out->BufferLength());
            BMessage parsedJson;
            BMessage errorObj;
            if (rawError.Length() > 0 
                && BJson::Parse(rawError.String(), parsedJson) == B_OK 
                && parsedJson.FindMessage("error", &errorObj) == B_OK) {
                const char* errMsg = errorObj.FindString("message");
                if (errMsg) {
                    errorReport.AddString("error_message", errMsg);
                } else {
                    errorReport.AddString("error_message", rawError.String());
                }
            } else {
                if (statusCode != 0) {
                    BString genericErr;
                    genericErr.SetToFormat("Impossibile recuperare i modelli. Errore HTTP %" B_PRId32, statusCode);
                    errorReport.AddString("error_message", genericErr.String());
                } else {
                    errorReport.AddString("error_message", "Errore di connessione durante il recupero dei modelli.");
                }
            }

            serverMessenger.SendMessage(&errorReport);
        }

        delete req; 
        delete out;

        if (strlen(defaultFallback) + 1 > out_len) return B_ERROR;
        strcpy(out_buf, defaultFallback);
        return B_OK;
    }

    BString jsonResponse((const char*)out->Buffer(), out->BufferLength());
    BString cleanJson("[");
    int32 currentPos = 0;
    bool first = true;

    while (true) {
        int32 matchPos = jsonResponse.FindFirst("\"id\": \"", currentPos);
        if (matchPos == B_ERROR) matchPos = jsonResponse.FindFirst("\"id\":\"" , currentPos);
        if (matchPos == B_ERROR) break;

        matchPos = jsonResponse.FindFirst("\"", matchPos + 4) + 1; 
        int32 endPos = jsonResponse.FindFirst("\"", matchPos);
        if (endPos == B_ERROR) break;

        BString modelName;
        jsonResponse.CopyInto(modelName, matchPos, endPos - matchPos);

        if (!first) {
            cleanJson.Append(", ");
        }
        cleanJson << "\"" << modelName << "\"";
        first = false;

        currentPos = endPos + 1;
    }
    cleanJson.Append("]");

    if (cleanJson == "[]") {
        cleanJson = defaultFallback;
    }

    size_t jsonLength = (size_t)cleanJson.Length();
    size_t copy_len = jsonLength < out_len - 1 ? jsonLength : out_len - 1;
    
    memcpy(out_buf, cleanJson.String(), copy_len);
    out_buf[copy_len] = '\0';

    delete req; 
    delete out;
    return B_OK;
}

extern "C" const char* get_plugin_name() {
    return "PublicAIPlugin";
}

extern "C" uint32 ai_plugin_get_capabilities() {
    return AI_CAP_STREAMING ; 
}
