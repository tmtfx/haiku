// openai_plugin.cpp
// Plugin per il servizio OpenAI AI su Haiku - Versione Stateless Concorrente Nattiva.

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
/* usiamo la generica Handle in AIPlugin.h
struct OpenAIHandle {
    char* base_url;
};
*/

/*
struct OpenAIAsyncArgs {
    char* api_key;
    char* model;
    char* notify_path;
    char* base_url;
    BMessage* context_copy; 
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
            //
            //if (params.FindMessage("required", &requiredMsg) == B_OK) {
            //    const char* f = requiredMsg.FindString("0");
            //    if (f) reqFieldStr = f;
            //    params.RemoveName("required");
            //}
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

            // === LOG DI DEBUG SPECIFICO ===
            fprintf(stderr, "[DEBUG OpenAI TOOLS] Tool '%s' - jsonParams finale generato:\n%s\n", 
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

    // === LOG DEL PAYLOAD COMPLETO ===
    fprintf(stderr, "[DEBUG OpenAI TOOLS] PAYLOAD STRUMENTI COMPLETO INVIATO ALL'API:\n%s\n", outJson.String());
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

static char* dupstr_or_null(const char* s) {
    if (!s) return nullptr;
    size_t n = strlen(s) + 1;
    char* p = (char*)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

// Helper interno per serializzare la cronologia dei BMessage nel formato JSON di OpenAI
/*
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
    const char* systemPrompt = nullptr;
    if (chatContext) {
        chatContext->FindString("system_prompt", &systemPrompt);
    }
    if (systemPrompt && systemPrompt[0] != '\0') {
        BString escapedSystem = EscapeStringForJson(systemPrompt);
        outPayload << "    {\"role\": \"system\", \"content\": \"" << escapedSystem << "\"\}";
        first = false;
    }
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

*/
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
    const char* systemPrompt = nullptr;
    if (chatContext) {
        chatContext->FindString("system_prompt", &systemPrompt);
    }
    if (systemPrompt && systemPrompt[0] != '\0') {
        BString escapedSystem = EscapeStringForJson(systemPrompt);
        outPayload << "    {\"role\": \"system\", \"content\": \"" << escapedSystem << "\"\}";
        first = false;
    }
    BMessage historyMsg;
    
    // 1. Inietta lo storico se presente nel contesto nativo
    if (chatContext && chatContext->FindMessage("messages", &historyMsg) == B_OK) {
        int32 i = 0;
        BMessage msgTurn;
        while (historyMsg.FindMessage("msg", i++, &msgTurn) == B_OK) {
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
                // Messaggio standard di testo
                const char* role = nullptr;
                const char* content = nullptr;
                msgTurn.FindString("role", &role);
                if (msgTurn.FindString("content", &content) != B_OK) {
                    msgTurn.FindString("text", &content);
                }

                if (role && content) {
                    if (!first) outPayload.Append(",\n");
                    
                    BString escapedContent = EscapeStringForJson(content);
                    
                    BString modelLine;
                    modelLine.SetToFormat("    {\"role\": \"%s\", \"content\": \"%s\"}", role, escapedContent.String());
                    outPayload << modelLine;
                    first = false;
                }
            }
        }
    }

    // 2. Se viene passato un prompt esplicito (non ancora salvato nello storico), lo appende alla fine
    if (explicitPrompt && explicitPrompt[0] != '\0') {
        if (!first) outPayload.Append(",\n");
        BString escapedPrompt = EscapeStringForJson(explicitPrompt);
        
        BString modelLine;
        modelLine.SetToFormat("    {\"role\": \"user\", \"content\": \"%s\"}", escapedPrompt.String());
        outPayload << modelLine;
    }

    outPayload.Append("\n  ]\n}");
}

// --- INTERFACCIA C PUBBLICA ---

extern "C" ai_plugin_t ai_plugin_init(void)
{
    AIPluginHandle* h = new(std::nothrow) AIPluginHandle();
    return (ai_plugin_t)h;
}

extern "C" void ai_plugin_free(ai_plugin_t handle)
{
    AIPluginHandle* h = (AIPluginHandle*)handle;
    delete h;
}

extern "C" status_t ai_plugin_generate_text_sync(ai_plugin_t handle,
                                             const char* prompt,
                                             char* response_buf,
                                             size_t response_len,
                                             BMessage* config)
{
    AIPluginHandle* h = (AIPluginHandle*)handle;
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

/*
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
    AIPluginHandle* h = (AIPluginHandle*)handle;
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

*/

static status_t openai_stream_thread_func(void* data)
{
    fprintf(stderr, "[OPENAI STREAM WORKER] Thread avviato.\n");
    AsyncArgs* args = (AsyncArgs*)data;
    if (!args) return B_BAD_VALUE;

    BMessage replyTools;
    bool mcpActive = false;
    bool executionLoop = true;

    // Controllo di sicurezza sulla chiave API
    if (!args->api_key || args->api_key[0] == '\0') {
        fprintf(stderr, "[OPENAI STREAM WORKER] ERRORE CRITICO: API key assente!\n");
        if (args->notify_path) {
            BFile streamFile(args->notify_path, B_WRITE_ONLY | B_CREATE_FILE);
            BString endMarker = "<<STREAM_END>>";
            streamFile.Write(endMarker.String(), endMarker.Length());
        }
        goto thread_cleanup;
    }

    // === CONTROLLO CAPABILITY VIA MESSENGER ===
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

    // === CASO 1: MODALITÀ STANDARD (STREAMING NATIVO RETROCOMPATIBILE) ===
    if (!mcpActive) {
        goto fallback_to_standard;
    }

    // === CASO 2: MODALITÀ MCP ATTIVA (LOOP STRUMENTI VIA IPC MESSENGER) ===
    fprintf(stderr, "[OPENAI STREAM WORKER] Modalità MCP Attiva: Avvio loop di interazione strumenti.\n");
    
    executionLoop = true;
    while (executionLoop) {
        // Controllo interruzione preventivo via IPC
        if (args->server_messenger.IsValid()) {
            BMessage checkAbortMsg('CHAB'); // MSG_CHECK_ABORT
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
                    fprintf(stderr, "[OPENAI STREAM WORKER] Rilevata interruzione asincrona dall'utente. Esco.\n");
                    executionLoop = false;
                    break;
                }
            }
        }

        BString url = (args->base_url && args->base_url[0] != '\0') ? args->base_url : "https://api.openai.com/v1/chat/completions";

        BString payload;
        // In modalità MCP non usiamo lo streaming (stream = false)
        BuildOpenAIPayload(args->context_copy, nullptr, payload, false);

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
            fprintf(stderr, "[OPENAI STREAM WORKER] Fallito il parsing JSON della risposta di rete.\n");
            executionLoop = false;
            break;
        }
        
        BMessage errorObj;
        if (parsedJson.FindMessage("error", &errorObj) == B_OK) {
            const char* errMsg = nullptr;
            errorObj.FindString("message", &errMsg);
            
            // Se l'errore contiene la lamentela sui flag dei tool o sulle capability
            if (errMsg && (BString(errMsg).FindFirst("tool choice") != B_ERROR || 
                           BString(errMsg).FindFirst("tools") != B_ERROR)) {
                
                fprintf(stderr, "[OPENAI WORKER] Il server remoto rifiuta l'MCP. Errore: %s\n", errMsg);
                fprintf(stderr, "[OPENAI WORKER] Disattivazione MCP forzata e passaggio alla Modalità Standard.\n");
                
                // 1. Disattiviamo i flag dell'MCP
                mcpActive = false; 
                executionLoop = false; // Rompiamo il loop MCP attuale
                
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

            // Caso A: OpenAI vuole eseguire un tool
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

                fprintf(stderr, "[OPENAI MCP] L'LLM richiede lo strumento: %s con argomenti: %s\n", toolName, argsJson.String());

                // Invochiamo lo strumento mandando un messaggio sincrono all'ai_server
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

                // Aggiorniamo la history clonata in memoria per il prossimo turno del loop
                AppendToolCallToContext(args->context_copy, toolName, &argsMsg, toolCallId);
                AppendToolResponseToContext(args->context_copy, toolName, toolResultBuf.String(), toolCallId);
            } 
            // Caso B: OpenAI restituisce il testo finale
            else if (message.FindString("content", &textContent) == B_OK && textContent != nullptr) {
                fprintf(stderr, "[OPENAI MCP] Risposta testuale finale ricevuta.\n");
                BFile streamFile(args->notify_path, B_WRITE_ONLY | B_CREATE_FILE | B_OPEN_AT_END);
                if (streamFile.InitCheck() == B_OK) {
                    streamFile.Write(textContent, strlen(textContent));
                } else {
                    fprintf(stderr, "[OPENAI MCP] ERRORE: Impossibile creare/aprire il file di notifica!\n");
                }
                executionLoop = false; // Usciamo dal loop!
            }
        } else {
            fprintf(stderr, "[OPENAI STREAM WORKER] ERRORE: Risposta di rete non valida o priva di 'choices'.\n");
            fprintf(stderr, "[OPENAI STREAM WORKER] Risposta grezza ricevuta:\n%s\n", rawResponse.String());
            
            BMessage errorObj;
            if (parsedJson.FindMessage("error", &errorObj) == B_OK) {
                const char* errMsg = nullptr;
                errorObj.FindString("message", &errMsg);
                if (errMsg) {
                    fprintf(stderr, "[OPENAI STREAM WORKER] Dettaglio errore API OpenAI: %s\n", errMsg);
                    
                    BFile streamFile(args->notify_path, B_WRITE_ONLY | B_OPEN_AT_END);
                    if (streamFile.InitCheck() == B_OK) {
                        BString guiError;
                        guiError.SetToFormat("\n[Errore API OpenAI: %s]\n", errMsg);
                        streamFile.Write(guiError.String(), guiError.Length());
                    }
                }
            }
            executionLoop = false;
        }
    }
    goto thread_post_actions;

 
    // =========================================================================
    // === CASO 1: MODALITÀ STANDARD (STREAMING NATIVO RETROCOMPATIBILE) ===
    // =========================================================================
fallback_to_standard:
{
    fprintf(stderr, "[OPENAI STREAM WORKER] Modalità Standard: Avvio Streaming Diretto.\n");
    
    BString url = (args->base_url && args->base_url[0] != '\0') ? args->base_url : "https://api.openai.com/v1/chat/completions";

    BString payload;
    // Forziamo stream = true e non passiamo alcun tool
    BuildOpenAIPayload(args->context_copy, nullptr, payload, true);

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
// =========================================================================
// === AZIONI POST-RISPOSTA (CONVERGENZA PER ENTRAMBI I FLUSSI) ===
// =========================================================================
thread_post_actions:
    // === GENERAZIONE TITOLO POST-RISPOSTA IN MODALITÀ ASINCRONA ===
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
            fprintf(stderr, "[OPENAI ASYNC] Auto-titolo generato: '%s'\n", autoTitle.String());
        }
    }
    // Scriviamo il terminatore sul file per svegliare il server
    {
        BFile streamFile(args->notify_path, B_WRITE_ONLY | B_CREATE_FILE | B_OPEN_AT_END);
        if (streamFile.InitCheck() == B_OK) {
            BString endMarker = "<<STREAM_END>>";
            streamFile.Write(endMarker.String(), endMarker.Length());
        }
    }

thread_cleanup:
    fprintf(stderr, "[OPENAI STREAM WORKER] Pulizia e chiusura thread worker.\n");
    delete args; 
    return B_OK;
}

extern "C" status_t ai_plugin_generate_text_async(ai_plugin_t handle, const char* prompt, BMessage* config)
{
    AIPluginHandle* h = (AIPluginHandle*)handle;
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
    
    AsyncArgs* args = new (std::nothrow) AsyncArgs();
    if (!args) return B_ERROR;
    
    args->api_key = dupstr_or_null(apiKey);
    args->model = dupstr_or_null(modelName && modelName[0] ? modelName : "gpt-4o-mini");
    args->notify_path = dupstr_or_null(notifyPath);
    args->base_url = h ? dupstr_or_null(h->base_url) : nullptr;
    args->context_copy = new BMessage(*config);

    BMessenger serverMessenger;
    if (config->FindMessenger("server_messenger", &serverMessenger) == B_OK) {
        args->server_messenger = serverMessenger;
    } else {
        args->server_messenger = BMessenger();
    }

    thread_id thread = spawn_thread(
        openai_stream_thread_func,
        "openai_stream_worker",
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

/*
uint32 ai_plugin_get_capabilities() {
    return AI_CAP_STREAMING | AI_CAP_REMOTE_CONTEXT; 
}
*/
extern "C" uint32 ai_plugin_get_capabilities() {
    return AI_CAP_STREAMING | AI_CAP_REMOTE_CONTEXT | AI_CAP_MCP; 
}
