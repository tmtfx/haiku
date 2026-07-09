// mistral_plugin.cpp
// Plugin Mistral AI per Haiku - implementazione OpenAI-compatible con streaming SSE.

#include <os/ai/AIPlugin.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <Url.h>
#include <UrlProtocolRoster.h>
#include <UrlRequest.h>
#include <UrlSynchronousRequest.h>
#include <UrlProtocolListener.h>
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

struct MistralHandle {
    char* base_url;
};

struct MistralAsyncArgs {
    char* api_key;
    char* model;
    char* notify_path;
    char* base_url;
    BMessage* context_copy;
};

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

extern "C" uint32 ai_plugin_get_capabilities(void)
{
    return AI_CAP_STREAMING;
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
