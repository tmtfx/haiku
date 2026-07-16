// mistral_plugin.cpp
// Plugin Mistral AI per Haiku - implementazione OpenAI-compatible con streaming SSE.

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
//#include <UrlProtocolListener.h>
#include <DataIO.h>
#include <HttpHeaders.h>
#include <HttpRequest.h>
#include <String.h>
#include <File.h>
#include <OS.h>

#include <Json.h>
#include <HttpResult.h>

using namespace BPrivate::Network;

#define DEFAULT_MISTRAL_URL   "https://api.mistral.ai/v1"
#define DEFAULT_MISTRAL_MODEL "mistral-large-latest"
#define MISTRAL_USER_AGENT    "HaikuAIEngine/1.0"

class StreamTarget : public BDataIO {
public:
    StreamTarget(const char* notifyPath)
    {
        fFile.SetTo(notifyPath, B_WRITE_ONLY | B_CREATE_FILE | B_OPEN_AT_END);
    }

    virtual ssize_t Write(const void* buffer, size_t size) override
    {
        if (fFile.InitCheck() != B_OK || size == 0)
            return size;

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

            if (line.IsEmpty())
                continue;
            if (line == "data: [DONE]")
                break;

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

        return size;
    }

private:
    BFile   fFile;
    BString fBuffer;
};

/*
class CompletionListener : public BUrlProtocolListener {
public:
    CompletionListener(const char* notifyPath)
        : fPath(notifyPath)
    {
    }

    virtual void RequestCompleted(BUrlRequest* caller, bool success) override
    {
        BFile file(fPath.String(), B_WRITE_ONLY | B_OPEN_AT_END);
        if (file.InitCheck() == B_OK) {
            BString endMarker = "<<STREAM_END>>";
            file.Write(endMarker.String(), endMarker.Length());
        }
    }

private:
    BString fPath;
};
*/

struct MistralHandle {
    char* base_url;
};

/*
struct MistralAsyncArgs {
    char* api_key;
    char* model;
    char* notify_path;
    char* base_url;
    BMessage* context_copy;
};
*/

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

/*
class SyncListener : public BUrlProtocolListener {
public:
    SyncListener()
    {
    }

    virtual ~SyncListener()
    {
    }

    bool CertificateVerificationFailed(BUrlRequest* request, BCertificate& certificate,
        const char* message) override
    {
        return false;
    }
};
*/

static BString EscapeStringForJson(const char* input) {
    if (!input) return "";
    BString escaped;
    const char* p = input;
    while (*p) {
        switch (*p) {
            case '\\': escaped << "\\\\"; break;
            case '"':  escaped << "\\\""; break;
            case '\n': escaped << "\\n"; break;
            case '\r': escaped << "\\r"; break;
            case '\t': escaped << "\\t"; break;
            default:   escaped << *p; break;
        }
        p++;
    }
    return escaped;
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
                BString escaped(strVal);
                escaped.ReplaceAll("\\", "\\\\");
                escaped.ReplaceAll("\"", "\\\"");
                escaped.ReplaceAll("\n", "\\n");
                escaped.ReplaceAll("\r", "\\r");
                escaped.ReplaceAll("\t", "\\t");
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

        BString escapedDesc(desc ? desc : "");
        escapedDesc.ReplaceAll("\\", "\\\\");
        escapedDesc.ReplaceAll("\"", "\\\"");
        escapedDesc.ReplaceAll("\n", "\\n");
        escapedDesc.ReplaceAll("\r", "\\r");
        escapedDesc.ReplaceAll("\t", "\\t");

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

            fprintf(stderr, "[DEBUG Mistral TOOLS] Tool '%s' - jsonParams finale generato:\n%s\n", 
                    name, jsonParams.String());

            outJson << ",\"parameters\":" << jsonParams;
        } else {
            outJson << ",\"parameters\":{\"type\":\"object\",\"properties\":{}}";
        }

        outJson << "}"; // fine function
        outJson << "}"; // fine tool
        i++;
    }

    outJson << "]";
    fprintf(stderr, "[DEBUG Mistral TOOLS] PAYLOAD STRUMENTI COMPLETO INVIATO ALL'API:\n%s\n", outJson.String());
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

/*
static void
BuildPayloadFromContext(const BMessage* config, const char* currentPrompt,
    BString& outPayload, bool stream)
{
    outPayload.SetTo("{\n");

    const char* model = nullptr;
    if (config)
        config->FindString("model_name", &model);
    if (!model || model[0] == '\0')
        model = DEFAULT_MISTRAL_MODEL;

    BString modelLine;
    modelLine.SetToFormat("  \"model\": \"%s\",\n", model);
    outPayload << modelLine;

    if (stream)
        outPayload.Append("  \"stream\": true,\n");

    outPayload.Append("  \"messages\": [");
    bool first = true;

    BMessage messagesMsg;
    if (config && config->FindMessage("messages", &messagesMsg) == B_OK) {
        BMessage turn;
        for (int32 i = 0; messagesMsg.FindMessage("msg", i, &turn) == B_OK; i++) {
            const char* role = nullptr;
            const char* content = nullptr;
            turn.FindString("role", &role);
            if (turn.FindString("content", &content) != B_OK)
                turn.FindString("text", &content);

            if (!role || !content || content[0] == '\0')
                continue;

            BString mappedRole = strcmp(role, "model") == 0 ? "assistant" : role;
            BString escapedContent(content);
            escapedContent.ReplaceAll("\\", "\\\\");
            escapedContent.ReplaceAll("\"", "\\\"");
            escapedContent.ReplaceAll("\n", "\\n");
            escapedContent.ReplaceAll("\r", "\\r");
            escapedContent.ReplaceAll("\t", "\\t");

            if (!first)
                outPayload.Append(",");

            BString objectStr;
            objectStr.SetToFormat("{\"role\":\"%s\",\"content\":\"%s\"}",
                mappedRole.String(), escapedContent.String());
            outPayload.Append(objectStr);
            first = false;
        }
    }

    if (first && currentPrompt && currentPrompt[0] != '\0') {
        BString escapedPrompt(currentPrompt);
        escapedPrompt.ReplaceAll("\\", "\\\\");
        escapedPrompt.ReplaceAll("\"", "\\\"");
        escapedPrompt.ReplaceAll("\n", "\\n");
        escapedPrompt.ReplaceAll("\r", "\\r");
        escapedPrompt.ReplaceAll("\t", "\\t");

        BString objectStr;
        objectStr.SetToFormat("{\"role\":\"user\",\"content\":\"%s\"}",
            escapedPrompt.String());
        outPayload.Append(objectStr);
    }

    outPayload.Append("]\n}");
}
*/

static void
BuildPayloadFromContext(const BMessage* config, const char* currentPrompt,
    BString& outPayload, bool stream)
{
    const char* model = nullptr;
    if (config)
        config->FindString("model_name", &model);
    if (!model || model[0] == '\0')
        model = DEFAULT_MISTRAL_MODEL;

    outPayload.SetTo("{\n");
    BString modelLine;
    modelLine.SetToFormat("  \"model\": \"%s\",\n", model);
    outPayload << modelLine;

    if (stream)
        outPayload.Append("  \"stream\": true,\n");

    outPayload.Append("  \"messages\": [\n");
    bool first = true;

    BMessage messagesMsg;
    if (config && config->FindMessage("messages", &messagesMsg) == B_OK) {
        int32 i = 0;
        BMessage msgTurn;
        while (messagesMsg.FindMessage("msg", i++, &msgTurn) == B_OK) {
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
                
                cleanArgs.ReplaceAll("\n", "\\n");
                cleanArgs.ReplaceAll("\r", "\\r");

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

                if (role && content) {
                    if (!first) outPayload.Append(",\n");
                    
                    BString mappedRole = strcmp(role, "model") == 0 ? "assistant" : role;
                    BString escapedContent = EscapeStringForJson(content);
                    
                    BString objectStr;
                    objectStr.SetToFormat("    {\"role\": \"%s\", \"content\": \"%s\"}",
                        mappedRole.String(), escapedContent.String());
                    outPayload.Append(objectStr);
                    first = false;
                }
            }
        }
    }

    if (first && currentPrompt && currentPrompt[0] != '\0') {
        BString escapedPrompt = EscapeStringForJson(currentPrompt);
        BString objectStr;
        objectStr.SetToFormat("    {\"role\": \"user\", \"content\": \"%s\"}",
            escapedPrompt.String());
        outPayload.Append(objectStr);
    }

    outPayload.Append("\n  ]\n}");
}

extern "C" ai_plugin_t ai_plugin_init(const BMessage* settingsMsg)
{
    MistralHandle* handle = (MistralHandle*)malloc(sizeof(MistralHandle));
    if (!handle)
        return nullptr;

    handle->base_url = nullptr;
    if (settingsMsg) {
        const char* url = nullptr;
        if (settingsMsg->FindString("base_url", &url) == B_OK && url && url[0] != '\0')
            handle->base_url = dupstr_or_null(url);
    }

    return (ai_plugin_t)handle;
}

extern "C" void ai_plugin_free(ai_plugin_t handle)
{
    MistralHandle* mistral = (MistralHandle*)handle;
    if (!mistral)
        return;

    if (mistral->base_url)
        free(mistral->base_url);
    free(mistral);
}

/*
extern "C" uint32 ai_plugin_get_capabilities(void)
{
    return AI_CAP_STREAMING;
}
*/
extern "C" uint32 ai_plugin_get_capabilities(void)
{
    return AI_CAP_STREAMING | AI_CAP_MCP;
}

extern "C" status_t ai_plugin_generate_text_sync(ai_plugin_t handle,
    const char* prompt, char* response_buf, size_t response_len,
    BMessage* contextMsg)
{
    fprintf(stderr, "[MISTRAL PLUGIN] generate_text_sync start\n");

    if (!response_buf || response_len == 0)
        return B_BAD_VALUE;
    response_buf[0] = '\0';

    if (!contextMsg) {
        fprintf(stderr, "[MISTRAL PLUGIN] missing context message\n");
        snprintf(response_buf, response_len, "[mistral_plugin] error: missing context");
        return B_BAD_VALUE;
    }

    const char* apiKey = nullptr;
    contextMsg->FindString("api_key", &apiKey);
    if (!apiKey || apiKey[0] == '\0') {
        fprintf(stderr, "[MISTRAL PLUGIN] missing api_key\n");
        snprintf(response_buf, response_len, "[mistral_plugin] error: no API key provided");
        return B_BAD_VALUE;
    }

    MistralHandle* mistral = (MistralHandle*)handle;
    BString url = (mistral && mistral->base_url && mistral->base_url[0] != '\0')
        ? mistral->base_url : DEFAULT_MISTRAL_URL;
    if (!url.EndsWith("/"))
        url.Append("/", 1);
    url.Append("chat/completions");

    BString payload;
    BuildPayloadFromContext(contextMsg, prompt, payload, false);

    BMallocIO* out = new BMallocIO();
    SyncListener listener;
    BUrl bUrl(url.String(), true);
    BUrlRequest* req = BUrlProtocolRoster::MakeRequest(bUrl, out, &listener, NULL);
    if (!req) {
        fprintf(stderr, "[MISTRAL PLUGIN] request creation failed\n");
        delete out;
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
        headers.AddHeader("User-Agent", MISTRAL_USER_AGENT);
        BString authHeader;
        authHeader.SetToFormat("Bearer %s", apiKey);
        headers.AddHeader("Authorization", authHeader.String());
        http->SetHeaders(headers);
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
    if (rc != B_OK || !buf || len == 0 || statusCode != 200) {
        fprintf(stderr, "[MISTRAL PLUGIN] request failed rc=%ld status=%ld len=%lu\n",
            (long)rc, (long)statusCode, (unsigned long)len);
        if (buf && len > 0)
            snprintf(response_buf, response_len, "%.*s", (int)len, (const char*)buf);
        else
            snprintf(response_buf, response_len, "[mistral_plugin] error: request failed");
        delete req;
        delete out;
        return B_ERROR;
    }

    bool extracted = false;
    BString rawResponse((const char*)buf, len);
    BMessage parsedJson;
    if (BJson::Parse(rawResponse.String(), parsedJson) == B_OK) {
        BMessage choices;
        BMessage choiceZero;
        BMessage message;
        const char* content = nullptr;
        if (parsedJson.FindMessage("choices", &choices) == B_OK
            && choices.FindMessage("0", &choiceZero) == B_OK
            && choiceZero.FindMessage("message", &message) == B_OK
            && message.FindString("content", &content) == B_OK) {
            size_t textLen = strlen(content);
            size_t copyLen = textLen < response_len - 1 ? textLen : response_len - 1;
            memcpy(response_buf, content, copyLen);
            response_buf[copyLen] = '\0';
            extracted = true;
        }
    }

    if (!extracted) {
        size_t copyLen = len < response_len - 1 ? len : response_len - 1;
        memcpy(response_buf, buf, copyLen);
        response_buf[copyLen] = '\0';
    }

    delete req;
    delete out;
    fprintf(stderr, "[MISTRAL PLUGIN] generate_text_sync end\n");
    return B_OK;
}

/*
static int32 mistral_stream_thread_func(void* data)
{
    MistralAsyncArgs* args = (MistralAsyncArgs*)data;
    if (!args)
        return B_BAD_VALUE;

    fprintf(stderr, "[MISTRAL PLUGIN] async worker start\n");

    if (args->context_copy && args->model && args->model[0] != '\0') {
        if (args->context_copy->ReplaceString("model_name", args->model) != B_OK)
            args->context_copy->AddString("model_name", args->model);
    }

    if (!args->api_key || args->api_key[0] == '\0' || !args->notify_path
        || args->notify_path[0] == '\0') {
        fprintf(stderr, "[MISTRAL PLUGIN] async worker missing required fields\n");
        if (args->notify_path && args->notify_path[0] != '\0') {
            BFile streamFile(args->notify_path, B_WRITE_ONLY | B_CREATE_FILE);
            BString endMarker = "<<STREAM_END>>";
            streamFile.Write(endMarker.String(), endMarker.Length());
        }
        if (args->api_key) free(args->api_key);
        if (args->model) free(args->model);
        if (args->notify_path) free(args->notify_path);
        if (args->base_url) free(args->base_url);
        if (args->context_copy) delete args->context_copy;
        free(args);
        return B_ERROR;
    }

    BString urlString = (args->base_url && args->base_url[0] != '\0')
        ? args->base_url : DEFAULT_MISTRAL_URL;
    if (!urlString.EndsWith("/"))
        urlString.Append("/", 1);
    urlString.Append("chat/completions");

    BString payload;
    BuildPayloadFromContext(args->context_copy, nullptr, payload, true);

    {
        StreamTarget streamTarget(args->notify_path);
        CompletionListener listener(args->notify_path);
        BUrl bUrl(urlString.String(), false);
        BUrlRequest* req = BUrlProtocolRoster::MakeRequest(bUrl, &streamTarget, &listener, NULL);
        if (req) {
            BHttpRequest* http = dynamic_cast<BHttpRequest*>(req);
            if (http) {
                http->SetMethod(B_HTTP_POST);
                BHttpHeaders headers;
                headers.AddHeader("Content-Type", "application/json");
                headers.AddHeader("User-Agent", MISTRAL_USER_AGENT);
                BString authHeader;
                authHeader << "Bearer " << args->api_key;
                headers.AddHeader("Authorization", authHeader.String());
                http->SetHeaders(headers);

                BMemoryIO* input = new BMemoryIO(payload.String(), payload.Length());
                http->AdoptInputData(input, payload.Length());
            }

            thread_id thread = req->Run();
            if (thread >= 0) {
                status_t rc = B_ERROR;
                wait_for_thread(thread, &rc);
                fprintf(stderr, "[MISTRAL PLUGIN] async worker completed rc=%ld\n", (long)rc);
            }
            delete req;
        } else {
            fprintf(stderr, "[MISTRAL PLUGIN] async request creation failed\n");
            BFile streamFile(args->notify_path, B_WRITE_ONLY | B_CREATE_FILE);
            BString endMarker = "<<STREAM_END>>";
            streamFile.Write(endMarker.String(), endMarker.Length());
        }
    }

    if (args->api_key) free(args->api_key);
    if (args->model) free(args->model);
    if (args->notify_path) free(args->notify_path);
    if (args->base_url) free(args->base_url);
    if (args->context_copy) delete args->context_copy;
    free(args);
    return B_OK;
}
*/

static int32 mistral_stream_thread_func(void* data)
{
    fprintf(stderr, "[MISTRAL STREAM WORKER] Thread avviato.\n");
    AsyncArgs* args = (AsyncArgs*)data;
    if (!args) return B_BAD_VALUE;

    BMessage replyTools;
    bool mcpActive = false;
    bool executionLoop = true;

    if (!args->api_key || args->api_key[0] == '\0') {
        fprintf(stderr, "[MISTRAL STREAM WORKER] ERRORE CRITICO: API key assente!\n");
        if (args->notify_path) {
            BFile streamFile(args->notify_path, B_WRITE_ONLY | B_CREATE_FILE);
            BString endMarker = "<<STREAM_END>>";
            streamFile.Write(endMarker.String(), endMarker.Length());
        }
        goto thread_cleanup;
    }

    if (args->server_messenger.IsValid()) {
        BMessage reqTools(MSG_MCP_GET_TOOLS); 
        const char* ctxId = nullptr;
        if (args->context_copy) {
            args->context_copy->FindString("context_id", &ctxId);
        }
        if (ctxId) {
            reqTools.AddString("context_id", ctxId);
        }
        
        if (args->server_messenger.SendMessage(&reqTools, &replyTools) == B_OK) {
            if (replyTools.HasMessage("tool", 0)) {
                mcpActive = true;
            }
        }
    }

    if (!mcpActive) {
        goto fallback_to_standard;
    }

    fprintf(stderr, "[MISTRAL STREAM WORKER] Modalità MCP Attiva: Avvio loop di interazione strumenti.\n");
    
    executionLoop = true;
    while (executionLoop) {
        if (args->server_messenger.IsValid()) {
            BMessage checkAbortMsg('CHAB');
            const char* ctxId = nullptr;
            if (args->context_copy) {
                args->context_copy->FindString("context_id", &ctxId);
            }
            if (ctxId) {
                checkAbortMsg.AddString("context_id", ctxId);
            }
            
            BMessage abortReply;
            if (args->server_messenger.SendMessage(&checkAbortMsg, &abortReply) == B_OK) {
                int32 status = B_OK;
                if (abortReply.FindInt32("status", &status) == B_OK && status == B_CANCELED) {
                    fprintf(stderr, "[MISTRAL STREAM WORKER] Rilevata interruzione asincrona dall'utente. Esco.\n");
                    executionLoop = false;
                    break;
                }
            }
        }

        BString url = (args->base_url && args->base_url[0] != '\0') ? args->base_url : DEFAULT_MISTRAL_URL;
        if (!url.EndsWith("/"))
            url.Append("/", 1);
        url.Append("chat/completions");

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
            executionLoop = false; 
            break; 
        }

        BHttpRequest* http = dynamic_cast<BHttpRequest*>(req);
        if (http) {
            http->SetMethod(B_HTTP_POST);
            BHttpHeaders headers;
            headers.AddHeader("Content-Type", "application/json");
            headers.AddHeader("User-Agent", MISTRAL_USER_AGENT);
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
        delete req;

        BString rawResponse((const char*)outNetworkData.Buffer(), outNetworkData.BufferLength());
        BMessage parsedJson;
        
        if (BJson::Parse(rawResponse.String(), parsedJson) != B_OK) {
            fprintf(stderr, "[MISTRAL STREAM WORKER] Fallito il parsing JSON della risposta di rete.\n");
            executionLoop = false;
            break;
        }
        
        BMessage errorObj;
        if (parsedJson.FindMessage("error", &errorObj) == B_OK) {
            const char* errMsg = nullptr;
            errorObj.FindString("message", &errMsg);
            
            if (errMsg && (BString(errMsg).FindFirst("tool choice") != B_ERROR || 
                           BString(errMsg).FindFirst("tools") != B_ERROR)) {
                
                fprintf(stderr, "[MISTRAL WORKER] Il server remoto rifiuta l'MCP. Errore: %s\n", errMsg);
                mcpActive = false; 
                executionLoop = false;
                goto fallback_to_standard;
            }
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
                BString argsJson;
                if (argumentsStr && BJson::Parse(argumentsStr, argsMsg) == B_OK) {
                    SerializeBMessageToJson(&argsMsg, argsJson);
                } else {
                    argsJson = "{}";
                }

                fprintf(stderr, "[MISTRAL MCP] L'LLM richiede lo strumento: %s con argomenti: %s\n", toolName, argsJson.String());

                BMessage reqExec(MSG_EXECUTE_TOOL);
                reqExec.AddString("name", toolName);
                reqExec.AddMessage("arguments", &argsMsg);
                
                const char* ctxId = nullptr;
                if (args->context_copy) {
                    args->context_copy->FindString("context_id", &ctxId);
                }
                if (ctxId) {
                    reqExec.AddString("context_id", ctxId);
                }
                
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
                fprintf(stderr, "[MISTRAL MCP] Risposta testuale finale ricevuta.\n");
                BFile streamFile(args->notify_path, B_WRITE_ONLY | B_CREATE_FILE | B_OPEN_AT_END);
                if (streamFile.InitCheck() == B_OK) {
                    streamFile.Write(textContent, strlen(textContent));
                }
                executionLoop = false;
            }
        } else {
            fprintf(stderr, "[MISTRAL STREAM WORKER] ERRORE: Risposta di rete non valida o priva di 'choices'.\n");
            executionLoop = false;
        }
    }
    goto thread_post_actions;

fallback_to_standard:
{
    fprintf(stderr, "[MISTRAL STREAM WORKER] Modalità Standard: Avvio Streaming Diretto.\n");
    
    BString url = (args->base_url && args->base_url[0] != '\0') ? args->base_url : DEFAULT_MISTRAL_URL;
    if (!url.EndsWith("/"))
        url.Append("/", 1);
    url.Append("chat/completions");

    BString payload;
    BuildPayloadFromContext(args->context_copy, nullptr, payload, true);

    {
        StreamTarget streamTarget(args->notify_path);
        CompletionListener listener(args->notify_path);
        BUrl bUrl(url.String(), true);
        BUrlRequest* req = BUrlProtocolRoster::MakeRequest(bUrl, &streamTarget, &listener, NULL);
        if (req) {
            BHttpRequest* http = dynamic_cast<BHttpRequest*>(req);
            if (http) {
                http->SetMethod(B_HTTP_POST);
                BHttpHeaders headers;
                headers.AddHeader("Content-Type", "application/json");
                headers.AddHeader("User-Agent", MISTRAL_USER_AGENT);
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
            delete req;
        }
    }
}

thread_post_actions:
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
            fprintf(stderr, "[MISTRAL ASYNC] Auto-titolo generato: '%s'\n", autoTitle.String());
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
    fprintf(stderr, "[MISTRAL STREAM WORKER] Pulizia e chiusura thread worker.\n");
    delete args; 
    return B_OK;
}

/*
extern "C" status_t ai_plugin_generate_text_async(ai_plugin_t handle,
    const char* prompt, BMessage* contextMsg)
{
    MistralHandle* mistral = (MistralHandle*)handle;
    if (!contextMsg)
        return B_BAD_VALUE;

    const char* apiKey = nullptr;
    const char* model = nullptr;
    const char* notifyPath = nullptr;
    contextMsg->FindString("api_key", &apiKey);
    contextMsg->FindString("model_name", &model);
    contextMsg->FindString("notify_path", &notifyPath);

    if (!notifyPath || notifyPath[0] == '\0')
        return B_BAD_VALUE;

    MistralAsyncArgs* args = (MistralAsyncArgs*)malloc(sizeof(MistralAsyncArgs));
    if (!args)
        return B_NO_MEMORY;

    args->api_key = dupstr_or_null(apiKey);
    args->model = dupstr_or_null((model && model[0] != '\0') ? model : DEFAULT_MISTRAL_MODEL);
    args->notify_path = dupstr_or_null(notifyPath);
    args->base_url = (mistral && mistral->base_url) ? dupstr_or_null(mistral->base_url) : nullptr;
    args->context_copy = new BMessage(*contextMsg);

    if (prompt && prompt[0] != '\0') {
        BMessage messagesMsg;
        BMessage firstTurn;
        bool hasHistory = args->context_copy->FindMessage("messages", &messagesMsg) == B_OK
            && messagesMsg.FindMessage("msg", 0, &firstTurn) == B_OK;

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

    thread_id thread = spawn_thread(mistral_stream_thread_func,
        "mistral_stream_worker", B_NORMAL_PRIORITY, args);
    if (thread < B_OK) {
        if (args->api_key) free(args->api_key);
        if (args->model) free(args->model);
        if (args->notify_path) free(args->notify_path);
        if (args->base_url) free(args->base_url);
        delete args->context_copy;
        free(args);
        return B_ERROR;
    }

    resume_thread(thread);
    return B_OK;
}
*/

extern "C" status_t ai_plugin_generate_text_async(ai_plugin_t handle,
    const char* prompt, BMessage* contextMsg)
{
    MistralHandle* mistral = (MistralHandle*)handle;
    if (!contextMsg)
        return B_BAD_VALUE;

    const char* apiKey = nullptr;
    const char* model = nullptr;
    const char* notifyPath = nullptr;
    contextMsg->FindString("api_key", &apiKey);
    contextMsg->FindString("model_name", &model);
    contextMsg->FindString("notify_path", &notifyPath);

    if (!notifyPath || notifyPath[0] == '\0')
        return B_BAD_VALUE;

    AsyncArgs* args = new (std::nothrow) AsyncArgs();
    if (!args)
        return B_NO_MEMORY;

    args->api_key = dupstr_or_null(apiKey);
    args->model = dupstr_or_null((model && model[0] != '\0') ? model : DEFAULT_MISTRAL_MODEL);
    args->notify_path = dupstr_or_null(notifyPath);
    args->base_url = (mistral && mistral->base_url) ? dupstr_or_null(mistral->base_url) : nullptr;
    args->context_copy = new BMessage(*contextMsg);

    BMessenger serverMessenger;
    if (contextMsg->FindMessenger("server_messenger", &serverMessenger) == B_OK) {
        args->server_messenger = serverMessenger;
    } else {
        args->server_messenger = BMessenger();
    }

    if (prompt && prompt[0] != '\0') {
        BMessage messagesMsg;
        BMessage firstTurn;
        bool hasHistory = args->context_copy->FindMessage("messages", &messagesMsg) == B_OK
            && messagesMsg.FindMessage("msg", 0, &firstTurn) == B_OK;

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

    thread_id thread = spawn_thread(mistral_stream_thread_func,
        "mistral_stream_worker", B_NORMAL_PRIORITY, args);
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
    const char* defaultFallback = "[\"mistral-large-latest\",\"mistral-small-latest\",\"open-mistral-7b\"]";

    if (!out_buf || out_len == 0)
        return B_BAD_VALUE;

    if (!settingsMsg) {
        if (strlen(defaultFallback) + 1 > out_len)
            return B_ERROR;
        strcpy(out_buf, defaultFallback);
        return B_OK;
    }

    const char* apiKey = nullptr;
    const char* baseUrl = nullptr;
    settingsMsg->FindString("api_key", &apiKey);
    settingsMsg->FindString("base_url", &baseUrl);

    if (!apiKey || apiKey[0] == '\0') {
        if (strlen(defaultFallback) + 1 > out_len)
            return B_ERROR;
        strcpy(out_buf, defaultFallback);
        return B_OK;
    }

    BString urlString = (baseUrl && baseUrl[0] != '\0') ? baseUrl : DEFAULT_MISTRAL_URL;
    if (!urlString.EndsWith("/"))
        urlString.Append("/", 1);
    urlString.Append("models");

    BMallocIO* out = new BMallocIO();
    SyncListener listener;
    BUrl bUrl(urlString.String(), false);
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
        headers.AddHeader("User-Agent", MISTRAL_USER_AGENT);
        BString authHeader;
        authHeader.SetToFormat("Bearer %s", apiKey);
        headers.AddHeader("Authorization", authHeader.String());
        http->SetHeaders(headers);
    }

    thread_id thread = req->Run();
    status_t rc = B_ERROR;
    if (thread >= 0)
        wait_for_thread(thread, &rc);
    else
        rc = thread;

    if (rc == B_OK && http) {
        const BHttpResult* httpResult = dynamic_cast<const BHttpResult*>(&(http->Result()));
        if (httpResult)
            statusCode = httpResult->StatusCode();
    }

    if (rc != B_OK || out->BufferLength() == 0 || statusCode != 200) {
        delete req;
        delete out;
        if (strlen(defaultFallback) + 1 > out_len)
            return B_ERROR;
        strcpy(out_buf, defaultFallback);
        return B_OK;
    }

    BString jsonResponse((const char*)out->Buffer(), out->BufferLength());
    BString cleanJson("[");
    int32 currentPos = 0;
    bool first = true;

    while (true) {
        int32 matchPos = jsonResponse.FindFirst("\"id\"", currentPos);
        if (matchPos == B_ERROR)
            break;

        int32 colonPos = jsonResponse.FindFirst(":", matchPos);
        if (colonPos == B_ERROR)
            break;

        int32 valueStart = jsonResponse.FindFirst("\"", colonPos);
        if (valueStart == B_ERROR)
            break;
        valueStart += 1;

        int32 valueEnd = jsonResponse.FindFirst("\"", valueStart);
        if (valueEnd == B_ERROR)
            break;

        BString modelName;
        jsonResponse.CopyInto(modelName, valueStart, valueEnd - valueStart);
        modelName.Trim();

        if (!modelName.IsEmpty()
            && modelName.FindFirst("embed") == B_ERROR
            && modelName.FindFirst("moderation") == B_ERROR) {
            if (!first)
                cleanJson.Append(", ");
            cleanJson << "\"" << modelName << "\"";
            first = false;
        }

        currentPos = valueEnd + 1;
    }

    cleanJson.Append("]");
    if (cleanJson == "[]")
        cleanJson = defaultFallback;

    size_t jsonLength = (size_t)cleanJson.Length();
    size_t copyLen = jsonLength < out_len - 1 ? jsonLength : out_len - 1;
    memcpy(out_buf, cleanJson.String(), copyLen);
    out_buf[copyLen] = '\0';

    delete req;
    delete out;
    return B_OK;
}

extern "C" status_t ai_plugin_set_model(ai_plugin_t handle, const char* model_name)
{
    if (!handle || !model_name || model_name[0] == '\0')
        return B_BAD_VALUE;

    fprintf(stderr, "[MISTRAL PLUGIN] set_model uses per-request context: %s\n", model_name);
    return B_OK;
}

extern "C" const char* get_plugin_name()
{
    return "Mistral AI";
}
