// openai_plugin.cpp
// Plugin per il servizio OpenAI AI su Haiku - Versione Stateless Concorrente Nattiva.

#include <os/ai/AIPlugin.h>
#include <os/ai/AINetworkPlugin.h>
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

/*
class SyncListener : public BUrlProtocolListener {
public:
    SyncListener() {}
    virtual ~SyncListener() {}

    bool CertificateVerificationFailed(BUrlRequest* request, BCertificate& certificate,
        const char* message) override
    {
        return false;
    }
};
*/
static bool json_extract_quoted_string(const BString& json, int32 start, BString& out)
{
    out.SetTo("");
    const char* data = json.String();
    int32 length = json.Length();
    bool escape = false;

    for (int32 i = start; i < length; ++i) {
        char c = data[i];
        if (escape) {
            switch (c) {
                case 'n': out.Append("\n", 1); break;
                case 'r': out.Append("\r", 1); break;
                case 't': out.Append("\t", 1); break;
                case '"': out.Append('"', 1); break;
                case '\\': out.Append('\\', 1); break;
                case '/': out.Append('/', 1); break;
                default: out.Append(c, 1); break;
            }
            escape = false;
            continue;
        }

        if (c == '\\') {
            escape = true;
            continue;
        }

        if (c == '"')
            return true;

        out.Append(c, 1);
    }

    return false;
}

static void copy_to_buffer(const char* text, char* outBuf, size_t outLen)
{
    if (outBuf == nullptr || outLen == 0)
        return;

    if (text == nullptr)
        text = "";

    size_t length = strlen(text);
    size_t copyLen = length < outLen - 1 ? length : outLen - 1;
    memcpy(outBuf, text, copyLen);
    outBuf[copyLen] = '\0';
}

static BString extract_assistant_instructions(const BMessage* chatContext)
{
    const char* instructions = nullptr;
    if (chatContext != nullptr) {
        if (chatContext->FindString("instructions", &instructions) == B_OK && instructions != nullptr)
            return BString(instructions);
        if (chatContext->FindString("system_prompt", &instructions) == B_OK && instructions != nullptr)
            return BString(instructions);
    }

    BString combined;
    BMessage historyMsg;
    if (chatContext == nullptr || chatContext->FindMessage("messages", &historyMsg) != B_OK)
        return combined;

    int32 i = 0;
    BMessage msgTurn;
    while (historyMsg.FindMessage("msg", i++, &msgTurn) == B_OK) {
        const char* role = nullptr;
        const char* content = nullptr;
        msgTurn.FindString("role", &role);
        msgTurn.FindString("content", &content);
        if (role == nullptr || content == nullptr || strcmp(role, "system") != 0)
            continue;

        if (combined.Length() > 0)
            combined.Append("\n\n");
        combined.Append(content);
    }

    return combined;
}

// Helper: escape string for JSON
static BString json_escape(const char* s)
{
    BString escaped;
    if (s == nullptr)
        return escaped;

    for (const char* p = s; *p; ++p) {
        if (*p == '"' || *p == '\\')
            escaped.Append('\\', 1).Append(*p, 1);
        else if (*p == '\n')
            escaped.Append("\\n", 2);
        else if (*p == '\r')
            escaped.Append("\\r", 2);
        else if (*p == '\t')
            escaped.Append("\\t", 2);
        else
            escaped.Append(*p, 1);
    }

    return escaped;
}

// Helper: extract top-level string field from JSON
// Cerca sia "key": "val" che "key":"val"
static BString json_get_string(const BString& json, const char* key)
{
    BString quotedKey;
    quotedKey.SetToFormat("\"%s\"", key != nullptr ? key : "");

    int32 keyPos = json.FindFirst(quotedKey.String());
    if (keyPos == B_ERROR)
        return BString();

    const char* data = json.String();
    int32 pos = keyPos + quotedKey.Length();
    int32 length = json.Length();
    while (pos < length && (data[pos] == ' ' || data[pos] == '\t' || data[pos] == '\r'
        || data[pos] == '\n')) {
        ++pos;
    }
    if (pos >= length || data[pos] != ':')
        return BString();

    ++pos;
    while (pos < length && (data[pos] == ' ' || data[pos] == '\t' || data[pos] == '\r'
        || data[pos] == '\n')) {
        ++pos;
    }
    if (pos >= length || data[pos] != '"')
        return BString();

    BString value;
    return json_extract_quoted_string(json, pos + 1, value) ? value : BString();
}


/*
// Helper: generic synchronous HTTP request
// method: "GET", "POST", "DELETE"
// bodyJson: nullptr per GET
// Ritorna HTTP status code, -1 su errore di rete
static int32 openai_http_request(const char* method, const char* url,
    const char* apiKey, const char* bodyJson, BString& outResponse)
{
    outResponse.SetTo("");

    BMallocIO* out = new BMallocIO();
    SyncListener listener;
    BUrlRequest* req = BUrlProtocolRoster::MakeRequest(BUrl(url, true), out, &listener, NULL);
    if (req == nullptr) {
        delete out;
        return -1;
    }

    BHttpRequest* http = dynamic_cast<BHttpRequest*>(req);
    if (http == nullptr) {
        delete req;
        delete out;
        return -1;
    }

    http->SetMethod(method != nullptr ? method : "GET");

    BHttpHeaders headers;
    headers.AddHeader("Content-Type", "application/json");
    if (apiKey != nullptr && apiKey[0] != '\0') {
        BString authHeader;
        authHeader.SetToFormat("Bearer %s", apiKey);
        headers.AddHeader("Authorization", authHeader.String());
    }
    headers.AddHeader("OpenAI-Beta", "assistants=v2");
    http->SetHeaders(headers);

    if (bodyJson != nullptr) {
        size_t bodyLen = strlen(bodyJson);
        BMallocIO* input = new BMallocIO();
        input->WriteExactly(bodyJson, bodyLen);
        http->AdoptInputData(input, bodyLen);
    }

    thread_id thread = req->Run();
    status_t rc = B_ERROR;
    if (thread >= 0)
        wait_for_thread(thread, &rc);
    else
        rc = thread;

    if (out->Buffer() != nullptr && out->BufferLength() > 0)
        outResponse.SetTo((const char*)out->Buffer(), out->BufferLength());

    int32 statusCode = -1;
    if (rc == B_OK) {
        const BHttpResult* httpResult = dynamic_cast<const BHttpResult*>(&(http->Result()));
        if (httpResult != nullptr)
            statusCode = httpResult->StatusCode();
    }

    delete req;
    delete out;
    return statusCode;
}*/
static int32 openai_http_request(const char* method, const char* url,
    const char* apiKey, const char* bodyJson, BString& outResponse)
{
    outResponse.SetTo("");

    BMallocIO* out = new BMallocIO();
    SyncListener listener;
    BUrlRequest* req = BUrlProtocolRoster::MakeRequest(BUrl(url, true), out, &listener, NULL);
    if (req == nullptr) {
        delete out;
        return -1;
    }

    BHttpRequest* http = dynamic_cast<BHttpRequest*>(req);
    if (http == nullptr) {
        delete req;
        delete out;
        return -1;
    }

    // Normalizziamo il metodo (default GET)
    BString httpMethod(method != nullptr ? method : "GET");
    httpMethod.ToUpper(); // Sicurezza per evitare "post" invece di "POST"
    http->SetMethod(httpMethod.String());

    BHttpHeaders headers;
    
    // Il Content-Type va messo SOLO se c'è effettivamente un payload (POST/PUT)
    if (bodyJson != nullptr && bodyJson[0] != '\0' && httpMethod != "GET") {
        headers.AddHeader("Content-Type", "application/json");
    }
    
    if (apiKey != nullptr && apiKey[0] != '\0') {
        BString authHeader;
        authHeader.SetToFormat("Bearer %s", apiKey);
        headers.AddHeader("Authorization", authHeader.String());
    }

    // Questo non fa male a prescindere, ma OpenAI lo vuole solo sulle API Assistants
    BString urlCheck(url);
    if (urlCheck.FindFirst("/v1/threads") != B_ERROR || urlCheck.FindFirst("/v1/assistants") != B_ERROR) {
        headers.AddHeader("OpenAI-Beta", "assistants=v2");
    }
    
    http->SetHeaders(headers);

    // Gestione del corpo della richiesta
    if (bodyJson != nullptr && bodyJson[0] != '\0' && httpMethod != "GET") {
        size_t bodyLen = strlen(bodyJson);
        BMallocIO* input = new BMallocIO();
        input->WriteExactly(bodyJson, bodyLen);
        http->AdoptInputData(input, bodyLen); // Passaggio di proprietà a Haiku
    }

    thread_id thread = req->Run();
    status_t rc = B_ERROR;
    if (thread >= 0) {
        wait_for_thread(thread, &rc);
    } else {
        rc = thread;
    }

    int32 statusCode = -1;
    
    // Leggiamo la risposta solo se la richiesta HTTP è stata completata a livello di rete
    if (rc == B_OK) {
        const BHttpResult* httpResult = dynamic_cast<const BHttpResult*>(&(http->Result()));
        if (httpResult != nullptr) {
            statusCode = httpResult->StatusCode();
        }
        
        if (out->Buffer() != nullptr && out->BufferLength() > 0) {
            outResponse.SetTo((const char*)out->Buffer(), out->BufferLength());
        }
    } else {
        // Errore interno del Network Kit (es: timeout, DNS fallito)
        outResponse.SetToFormat("[network_kit] error: thread failed with code %d", (int)rc);
    }

    delete req;
    delete out;
    return statusCode;
}

// Build OpenAI API URL: base + path (gestisce trailing slash)
static BString openai_url(const char* baseUrl, const char* path)
{
    BString base = (baseUrl != nullptr && baseUrl[0] != '\0') ? baseUrl : "https://api.openai.com";

    int32 versionPos = base.FindFirst("/v1/");
    if (versionPos != B_ERROR)
        base.Truncate(versionPos);
    else if (base.Length() >= 3 && strcmp(base.String() + base.Length() - 3, "/v1") == 0)
        base.Truncate(base.Length() - 3);

    while (base.Length() > 0 && base.String()[base.Length() - 1] == '/')
        base.Truncate(base.Length() - 1);

    BString full(base);
    if (path != nullptr && path[0] != '\0') {
        if (path[0] != '/')
            full.Append("/");
        full.Append(path);
    }
    return full;
}

// POST /v1/assistants → outId = "asst_xxx"
static bool openai_create_assistant(const char* apiKey, const char* baseUrl,
    const char* model, const char* instructions, BString& outId)
{
    BString body;
    body << "{\"model\":\"" << json_escape(model != nullptr ? model : "gpt-4o-mini")
        << "\",\"instructions\":\"" << json_escape(instructions != nullptr ? instructions : "")
        << "\"}";

    BString response;
    BString url = openai_url(baseUrl, "/v1/assistants");
    int32 status = openai_http_request("POST", url.String(), apiKey, body.String(), response);
    outId = json_get_string(response, "id");
    return status >= 200 && status < 300 && outId.Length() > 0;
}

// POST /v1/threads → outId = "thread_xxx"
static bool openai_create_thread(const char* apiKey, const char* baseUrl, BString& outId)
{
    BString response;
    BString url = openai_url(baseUrl, "/v1/threads");
    int32 status = openai_http_request("POST", url.String(), apiKey, "{}", response);
    outId = json_get_string(response, "id");
    return status >= 200 && status < 300 && outId.Length() > 0;
}

// POST /v1/threads/{threadId}/messages
static bool openai_add_message(const char* apiKey, const char* baseUrl,
    const char* threadId, const char* content)
{
    BString url = openai_url(baseUrl, "/v1/threads");
    url << "/" << threadId << "/messages";

    // Costruiamo il JSON secondo le specifiche Assistants v2 (Array di blocchi di testo)
    BString body;
    body << "{\n"
         << "  \"role\": \"user\",\n"
         << "  \"content\": [\n"
         << "    {\n"
         << "      \"type\": \"text\",\n"
         << "      \"text\": \"" << json_escape(content != nullptr ? content : "") << "\"\n"
         << "    }\n"
         << "  ]\n"
         << "}";

    BString response;
    int32 status = openai_http_request("POST", url.String(), apiKey, body.String(), response);
    
    // Se fallisce, stampiamo nello standard error la risposta del server per capire il motivo
    //if (status < 200 || status >= 300) {
    //    fprintf(stderr, "[DEBUG HELPER] openai_add_message fallito con HTTP %d. Risposta: %s\n", 
    //        (int)status, response.String());
    //}

    return status >= 200 && status < 300;
}

// POST /v1/threads/{threadId}/runs → outRunId = "run_xxx"
static bool openai_create_run(const char* apiKey, const char* baseUrl,
    const char* threadId, const char* assistantId, BString& outRunId)
{
    BString url = openai_url(baseUrl, "/v1/threads");
    url << "/" << threadId << "/runs";

    BString body;
    body << "{\"assistant_id\":\"" << json_escape(assistantId != nullptr ? assistantId : "")
        << "\"}";

    BString response;
    int32 status = openai_http_request("POST", url.String(), apiKey, body.String(), response);
    outRunId = json_get_string(response, "id");
    return status >= 200 && status < 300 && outRunId.Length() > 0;
}

// Poll GET /v1/threads/{threadId}/runs/{runId} → true se "completed"
static bool openai_poll_run(const char* apiKey, const char* baseUrl,
    const char* threadId, const char* runId, int maxSeconds = 120)
{
    BString url = openai_url(baseUrl, "/v1/threads");
    url << "/" << threadId << "/runs/" << runId;

    for (int i = 0; i < maxSeconds; ++i) {
        BString response;
        int32 status = openai_http_request("GET", url.String(), apiKey, nullptr, response);
        if (status < 200 || status >= 300)
            return false;

        BString runStatus = json_get_string(response, "status");
        if (runStatus == "completed")
            return true;
        if (runStatus == "failed" || runStatus == "cancelled" || runStatus == "expired"
            || runStatus == "incomplete" || runStatus == "requires_action") {
            return false;
        }

        snooze(1000000);
    }

    return false;
}

// GET /v1/threads/{threadId}/messages?limit=1&order=desc → estrai testo risposta
static bool openai_get_latest_message(const char* apiKey, const char* baseUrl,
    const char* threadId, char* outBuf, size_t outLen)
{
    if (outBuf == nullptr || outLen == 0)
        return false;

    BString url = openai_url(baseUrl, "/v1/threads");
    url << "/" << threadId << "/messages?limit=1&order=desc";

    BString response;
    int32 status = openai_http_request("GET", url.String(), apiKey, nullptr, response);
    if (status < 200 || status >= 300)
        return false;

    int32 valuePos = response.FindFirst("\"value\"");
    if (valuePos == B_ERROR)
        return false;

    const char* data = response.String();
    int32 pos = valuePos + 7;
    int32 length = response.Length();
    while (pos < length && (data[pos] == ' ' || data[pos] == '\t' || data[pos] == '\r'
        || data[pos] == '\n')) {
        ++pos;
    }
    if (pos >= length || data[pos] != ':')
        return false;

    ++pos;
    while (pos < length && (data[pos] == ' ' || data[pos] == '\t' || data[pos] == '\r'
        || data[pos] == '\n')) {
        ++pos;
    }
    if (pos >= length || data[pos] != '"')
        return false;

    BString value;
    if (!json_extract_quoted_string(response, pos + 1, value))
        return false;

    copy_to_buffer(value.String(), outBuf, outLen);
    return true;
}

class StreamTarget : public BDataIO {
public:
    StreamTarget(const char* notifyPath) {
        fFile.SetTo(notifyPath, B_WRITE_ONLY | B_CREATE_FILE | B_OPEN_AT_END);
    }

    virtual ssize_t Write(const void* buffer, size_t size) override {
        if (fFile.InitCheck() != B_OK || size == 0) return size;

        BString rawData((const char*)buffer, size);
        int32 currentPos = 0;

        while (true) {
            int32 matchPos = rawData.FindFirst("data: ", currentPos);
            if (matchPos == B_ERROR) break;
            
            matchPos += 6; 
            int32 endLine = rawData.FindFirst("\n", matchPos);
            if (endLine == B_ERROR) endLine = rawData.Length();

            BString jsonChunk;
            rawData.CopyInto(jsonChunk, matchPos, endLine - matchPos);
            jsonChunk.Trim();

            if (jsonChunk == "[DONE]") break;

            int32 contentPos = jsonChunk.FindFirst("\"content\": \"");
            if (contentPos != B_ERROR) {
                contentPos += 12;
                int32 endContent = jsonChunk.FindFirst("\"", contentPos);
                if (endContent != B_ERROR) {
                    BString token;
                    jsonChunk.CopyInto(token, contentPos, endContent - contentPos);
                    fFile.Write(token.String(), token.Length());
                }
            }
            currentPos = endLine + 1;
        }
        return size;
    }

private:
    BFile fFile;
};
/*
class CompletionListener : public BUrlProtocolListener {
public:
    CompletionListener(const char* notifyPath) : fPath(notifyPath) {}

    virtual void RequestCompleted(BUrlRequest* caller, bool success) override {
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
struct OpenAIHandle {
    char* base_url;
};

struct OpenAIAsyncArgs {
    char* api_key;
    char* model;
    char* notify_path;
    char* base_url;
    BMessage* context_copy; 
};

static char* dupstr_or_null(const char* s) {
    if (!s) return nullptr;
    size_t n = strlen(s) + 1;
    char* p = (char*)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

// Helper interno per serializzare la cronologia dei BMessage nel formato JSON di OpenAI
static void BuildOpenAIPayload(const BMessage* chatContext, const char* explicitPrompt, BString& outPayload, bool stream = false)
{
    const char* model = nullptr;
    if (chatContext) chatContext->FindString("model_name", &model);
    if (!model || model[0] == '\0') model = "gpt-4o-mini";

    outPayload.SetTo("{\n");
    BString modelLine;
    modelLine.SetToFormat("  \"model\": \"%s\",\n", model);
    outPayload << modelLine;
    
    if (stream) {
        outPayload.Append("  \"stream\": true,\n");
    }
    outPayload.Append("  \"messages\": [\n");

    bool first = true;
    BMessage historyMsg;
    
    // 1. Inietta lo storico se presente nel contesto nativo
    if (chatContext && chatContext->FindMessage("messages", &historyMsg) == B_OK) {
        int32 i = 0;
        BMessage msgTurn;
        while (historyMsg.FindMessage("msg", i++, &msgTurn) == B_OK) {
            const char* role = nullptr;
            const char* content = nullptr;
            msgTurn.FindString("role", &role);
            msgTurn.FindString("content", &content);

            if (role && content) {
                if (!first) outPayload.Append(",\n");
                
                BString escapedContent;
                // Gestione escape basica per evitare rotture JSON
                for (const char* p = content; *p; ++p) {
                    if (*p == '"' || *p == '\\') escapedContent.Append('\\', 1).Append(*p, 1);
                    else if (*p == '\n') escapedContent.Append("\\n", 2);
                    else if (*p == '\r') escapedContent.Append("\\r", 2);
                    else if (*p == '\t') escapedContent.Append("\\t", 2);
                    else escapedContent.Append(*p, 1);
                }
                
                BString modelLine;
                modelLine.SetToFormat("    {\"role\": \"%s\", \"content\": \"%s\"}", role, escapedContent.String());
                outPayload << modelLine;
                first = false;
            }
        }
    }

    // 2. Se viene passato un prompt esplicito (non ancora salvato nello storico), lo appende alla fine
    if (explicitPrompt && explicitPrompt[0] != '\0') {
        if (!first) outPayload.Append(",\n");
        BString escapedPrompt;
        for (const char* p = explicitPrompt; *p; ++p) {
            if (*p == '"' || *p == '\\') escapedPrompt.Append('\\', 1).Append(*p, 1);
            else if (*p == '\n') escapedPrompt.Append("\\n", 2);
            else if (*p == '\r') escapedPrompt.Append("\\r", 2);
            else if (*p == '\t') escapedPrompt.Append("\\t", 2);
            else escapedPrompt.Append(*p, 1);
        }
        
        BString modelLine;
        modelLine.SetToFormat("    {\"role\": \"user\", \"content\": \"%s\"}", escapedPrompt.String());
        outPayload << modelLine;
    }

    outPayload.Append("\n  ]\n}");
}

// --- INTERFACCIA C PUBBLICA ---

extern "C" ai_plugin_t ai_plugin_init(const BMessage* config)
{
    OpenAIHandle* h = (OpenAIHandle*)malloc(sizeof(OpenAIHandle));
    if (!h) return nullptr;
    h->base_url = nullptr;
    
    const char* url = nullptr;
    if (config && config->FindString("base_url", &url) == B_OK) {
        h->base_url = dupstr_or_null(url);
    }
    return (ai_plugin_t)h;
}

extern "C" void ai_plugin_free(ai_plugin_t handle)
{
    OpenAIHandle* h = (OpenAIHandle*)handle;
    if (!h) return;
    if (h->base_url) free(h->base_url);
    free(h);
}

extern "C" status_t ai_plugin_generate_text_sync(ai_plugin_t handle,
                                             const char* prompt,
                                             char* response_buf,
                                             size_t response_len,
                                             BMessage* config)
{
    OpenAIHandle* h = (OpenAIHandle*)handle;
    if (!config || !response_buf || response_len == 0) return B_ERROR;
    
    
    const char* apiKeyRaw = nullptr;
    config->FindString("api_key", &apiKeyRaw);

    if (!apiKeyRaw || apiKeyRaw[0] == '\0') {
        snprintf(response_buf, response_len, "[openai_plugin] error: no API key provided");
        return B_ERROR;
    }
    BString apiKey(apiKeyRaw); //use BString as later a remove message can shift offset of the api key

    bool useRemoteContext = false;
    config->FindBool("use_remote_context", &useRemoteContext);
    
    if (useRemoteContext) {
        const char* model = nullptr;
        config->FindString("model_name", &model);
        if (!model || model[0] == '\0')
            model = "gpt-4o-mini";

        BString assistantId;
        BString threadId;
        const char* remoteIdRaw = nullptr;
        if (config->FindString("remote_id", &remoteIdRaw) == B_OK && remoteIdRaw != nullptr
            && remoteIdRaw[0] != '\0') {
            BString remoteId(remoteIdRaw);
            int32 separator = remoteId.FindFirst("::");
            if (separator != B_ERROR) {
                remoteId.CopyInto(assistantId, 0, separator);
                remoteId.CopyInto(threadId, separator + 2, remoteId.Length() - separator - 2);
            }
        }

        // STEP 1: Creazione Assistant e Thread se non esistono
        if (assistantId.Length() == 0 || threadId.Length() == 0) {
            BString instructions = extract_assistant_instructions(config);
            
            if (!openai_create_assistant(apiKey.String(), h ? h->base_url : nullptr, model,
                    instructions.String(), assistantId)) {
                snprintf(response_buf, response_len, "[openai_plugin] error: assistant creation failed. API Key valida?");
                return B_ERROR;
            }
            
            if (!openai_create_thread(apiKey.String(), h ? h->base_url : nullptr, threadId)) {
                snprintf(response_buf, response_len, "[openai_plugin] error: thread creation failed");
                return B_ERROR;
            }
            
            BString remoteId;
            remoteId << assistantId << "::" << threadId;
            config->RemoveName("remote_id");
            config->AddString("remote_id", remoteId.String());
            
            if (!config->HasString("title") && prompt != nullptr) {
                BString autoTitle(prompt);
                if (autoTitle.Length() > 30) {
                    autoTitle.Truncate(30);
                    autoTitle << "...";
                }
                config->RemoveName("title"); // Per sicurezza, rimuoviamo vecchi tentativi vuoti
                config->AddString("title", autoTitle.String());
            }
            
        }

        // STEP 2: Aggiunta del messaggio dell'utente
        if (!openai_add_message(apiKey.String(), h ? h->base_url : nullptr, threadId.String(),
                prompt != nullptr ? prompt : "")) {
            snprintf(response_buf, response_len, "[openai_plugin] error: add message failed for thread %s", threadId.String());
            return B_ERROR;
        }

        // STEP 3: Creazione del Run
        BString runId;
        if (!openai_create_run(apiKey, h ? h->base_url : nullptr, threadId.String(),
                assistantId.String(), runId)) {
            snprintf(response_buf, response_len, "[openai_plugin] error: run creation failed");
            return B_ERROR;
        }

        // STEP 4: Polling del Run
        if (!openai_poll_run(apiKey, h ? h->base_url : nullptr, threadId.String(), runId.String())) {
            snprintf(response_buf, response_len, "[openai_plugin] error: run did not complete (failed/timeout)");
            return B_ERROR;
        }

        // STEP 5: Recupero del testo finale
        if (!openai_get_latest_message(apiKey, h ? h->base_url : nullptr, threadId.String(),
                response_buf, response_len)) {
            snprintf(response_buf, response_len, "[openai_plugin] error: response extraction failed");
            return B_ERROR;
        }

        return B_OK;
    }
    
    BString url = (h && h->base_url && h->base_url[0]) ? h->base_url : "https://api.openai.com/v1/chat/completions";
    
    BString payload;
    BuildOpenAIPayload(config, prompt, payload, false);
    
    BMallocIO* out = new BMallocIO();
    SyncListener listener;
    BUrl bUrl(url.String(), true);
    
    BUrlRequest* req = BUrlProtocolRoster::MakeRequest(bUrl, out, &listener, NULL);
    if (!req) { 
        delete out; 
        snprintf(response_buf, response_len, "{\"error\":\"request failed\"}"); 
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
        authHeader.SetToFormat("Bearer %s", apiKey.String());
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
        delete req; delete out; 
        return B_ERROR; 
    }
    
    int32 statusCode = 0;
    if (http) {
        const BHttpResult* httpResult = dynamic_cast<const BHttpResult*>(&(http->Result()));
        if (httpResult != nullptr) {
            statusCode = httpResult->StatusCode();
        }
    }
    
    bool extractedSuccessfully = false;
    BString rawResponse((const char*)buf, len);

    if (statusCode == 200) {
        BMessage parsedJson;
        if (BJson::Parse(rawResponse.String(), parsedJson) == B_OK) {
            BMessage choices, choiceZero, message;
            const char* extractedText = nullptr;

            if (parsedJson.FindMessage("choices", &choices) == B_OK
                && choices.FindMessage("0", &choiceZero) == B_OK
                && choiceZero.FindMessage("message", &message) == B_OK
                && message.FindString("content", &extractedText) == B_OK) {
                
                size_t textLen = strlen(extractedText);
                size_t copy_len = textLen < response_len - 1 ? textLen : response_len - 1;
                memcpy(response_buf, extractedText, copy_len);
                response_buf[copy_len] = '\0';
                extractedSuccessfully = true;
            }
        }
    }

    if (!extractedSuccessfully) {
        size_t copy_len = len < response_len - 1 ? len : response_len - 1;
        memcpy(response_buf, buf, copy_len);
        response_buf[copy_len] = '\0';
    }

    delete req; delete out;
    return rc == B_OK ? 0 : -1;
}

static status_t openai_stream_thread_func(void* data)
{
    OpenAIAsyncArgs* args = (OpenAIAsyncArgs*)data;
    if (!args) return B_BAD_VALUE;

    BString url = (args->base_url && args->base_url[0] != '\0') ? args->base_url : "https://api.openai.com/v1/chat/completions";

    BString payload;
    // Passiamo il contesto intero duplicato per generare la cronologia corretta in streaming
    BuildOpenAIPayload(args->context_copy, nullptr, payload, true);

    StreamTarget streamTarget(args->notify_path);
    CompletionListener listener(args->notify_path);
    BUrl bUrl(url.String(), true);

    BUrlRequest* req = BUrlProtocolRoster::MakeRequest(bUrl, &streamTarget, &listener, NULL);
    if (!req) {
        BFile streamFile(args->notify_path, B_WRITE_ONLY | B_CREATE_FILE);
        BString endMarker = "<<STREAM_END>>";
        streamFile.Write(endMarker.String(), endMarker.Length());
        goto cleanup;
    }

    {
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
    }

    delete req;

cleanup:
    if (args->api_key) free(args->api_key);
    if (args->model) free(args->model);
    if (args->notify_path) free(args->notify_path);
    if (args->base_url) free(args->base_url);
    if (args->context_copy) delete args->context_copy;
    free(args);

    return B_OK;
}

extern "C" status_t ai_plugin_generate_text_async(ai_plugin_t handle, const char* prompt, BMessage* config)
{
    OpenAIHandle* h = (OpenAIHandle*)handle;
    if (!config) return B_ERROR;
    
    const char* apiKey = nullptr;
    const char* modelName = nullptr;
    const char* notifyPath = nullptr;
    
    config->FindString("api_key", &apiKey);
    config->FindString("model_name", &modelName);
    config->FindString("notify_path", &notifyPath);

    if (!notifyPath || notifyPath[0] == '\0') {
        fprintf(stderr, "[OPENAI_PLUGIN] Errore: notify_path mancante nella sessione.\n");
        return B_ERROR;
    }
    
    OpenAIAsyncArgs* args = (OpenAIAsyncArgs*)malloc(sizeof(OpenAIAsyncArgs));
    if (!args) return B_ERROR;
    
    args->api_key = dupstr_or_null(apiKey);
    args->model = dupstr_or_null(modelName && modelName[0] ? modelName : "gpt-4o-mini");
    args->notify_path = dupstr_or_null(notifyPath);
    args->base_url = h ? dupstr_or_null(h->base_url) : nullptr;
    
    // Facciamo una copia profonda del BMessage di contesto per passarla in sicurezza al thread isolato
    args->context_copy = new BMessage(*config);

    thread_id thread = spawn_thread(
        openai_stream_thread_func,
        "openai_stream_worker",
        B_NORMAL_PRIORITY,
        args
    );

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

extern "C" status_t ai_plugin_list_models(const BMessage* config, char* out_buf, size_t out_len)
{
    const char* defaultFallback = "[\"gpt-4o\", \"gpt-4o-mini\", \"gpt-4-turbo\", \"gpt-3.5-turbo\"]";

    if (!config || !out_buf || out_len == 0) {
        if (out_buf && out_len > strlen(defaultFallback)) {
            strcpy(out_buf, defaultFallback);
            return B_OK;
        }
        return B_ERROR;
    }

    const char* apiKey = nullptr;
    const char* baseUrl = nullptr;
    config->FindString("api_key", &apiKey);
    config->FindString("base_url", &baseUrl);

    if (!apiKey || apiKey[0] == '\0') {
        if (strlen(defaultFallback) + 1 > out_len) return B_ERROR;
        strcpy(out_buf, defaultFallback);
        return B_OK;
    }

    BString url = (baseUrl && baseUrl[0] != '\0') ? baseUrl : "https://api.openai.com/v1/models";

    BMallocIO* out = new BMallocIO();
    SyncListener listener;
    BUrl bUrl(url.String(), true);

    BUrlRequest* req = BUrlProtocolRoster::MakeRequest(bUrl, out, &listener, NULL);
    if (!req) { delete out; return B_ERROR; }
    
    int32 statusCode = 0;
    BHttpRequest* http = dynamic_cast<BHttpRequest*>(req);
    if (http) {
        http->SetMethod(B_HTTP_GET);
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
    
    if (rc == B_OK && http != nullptr) {
        const BHttpResult* httpResult = dynamic_cast<const BHttpResult*>(&(http->Result()));
        if (httpResult != nullptr) {
            statusCode = httpResult->StatusCode();
        }
    }

    if (rc != B_OK || out->BufferLength() == 0 || statusCode != 200) {
        delete req; delete out;
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
        if (matchPos == B_ERROR) break;

        matchPos += 7; 
        int32 endPos = jsonResponse.FindFirst("\"", matchPos);
        if (endPos == B_ERROR) break;

        BString modelName;
        jsonResponse.CopyInto(modelName, matchPos, endPos - matchPos);

        bool isEmbedding = (modelName.FindFirst("embedding") != B_ERROR);
        bool isWhisper = (modelName.FindFirst("whisper") != B_ERROR);
        bool isTTS = (modelName.FindFirst("tts") != B_ERROR);
        bool isModeration = (modelName.FindFirst("moderation") != B_ERROR);

        if (!isEmbedding && !isWhisper && !isTTS && !isModeration) {
            if (!first) {
                cleanJson.Append(", ");
            }
            cleanJson << "\"" << modelName << "\"";
            first = false;
        }
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

    delete req; delete out;
    return B_OK;
}

extern "C" const char* get_plugin_name() {
    return "OpenAIPlugin";
}

uint32 ai_plugin_get_capabilities() {
    return AI_CAP_STREAMING | AI_CAP_REMOTE_CONTEXT; 
}
