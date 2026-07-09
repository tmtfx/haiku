// publicai_plugin.cpp
// Plugin per il servizio Public AI su Haiku - Versione Stateless Concorrente.

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

#define DEFAULT_PUBLICAI_URL   "https://api.publicai.co/v1"
#define DEFAULT_PUBLICAI_MODEL "swiss-ai/apertus-8b-instruct"
#define PUBLICAI_USER_AGENT    "HaikuAIEngine/1.0"

// ============================================================================
// PARSER FLUSSO STREAMING (COMPATIBILE SSE)
// ============================================================================
class StreamTarget : public BDataIO {
public:
    StreamTarget(const char* notifyPath) {
        fFile.SetTo(notifyPath, B_WRITE_ONLY | B_CREATE_FILE | B_OPEN_AT_END);
    }

    virtual ssize_t Write(const void* buffer, size_t size) override {
        if (fFile.InitCheck() != B_OK || size == 0) return size;

        fBuffer.Append((const char*)buffer, size);
        int32 processedPos = 0;

        while (true) {
            int32 nextNewline = fBuffer.FindFirst("\n", processedPos);
            if (nextNewline == B_ERROR) {
                break;
            }

            BString line;
            fBuffer.CopyInto(line, processedPos, nextNewline - processedPos);
            line.Trim(); 

            processedPos = nextNewline + 1;

            if (line.IsEmpty()) continue;

            if (line == "data: [DONE]") {
                break;
            }

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
                    
                    while (endContent != B_ERROR && line.ByteAt(endContent - 1) == '\\') {
                        endContent = line.FindFirst("\"", endContent + 1);
                    }

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

    if (processedPos > 0) {
        fBuffer.Remove(0, processedPos);
    }

    return size;
}

private:
    BFile   fFile;
    BString fBuffer;
};

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

struct PublicAIHandle {
    char* base_url;
};

struct PublicAIAsyncArgs {
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

class SyncListener : public BUrlProtocolListener {
public:
    SyncListener() {}
    virtual ~SyncListener() {}
    bool CertificateVerificationFailed(BUrlRequest* request, BCertificate& certificate, const char* message) override {
        return false; 
    }
};

// ============================================================================
// COSTRUZIONE PAYLOAD COMPATIBILE OPENAI / PUBLICAI
// ============================================================================
void BuildPayloadFromContext(const BMessage* config, const char* currentPrompt, BString& outPayload, bool stream = false)
{
    outPayload.SetTo("{\n");
    
    // Recupero modello
    const char* model = nullptr;
    if (config) config->FindString("model_name", &model);
    if (!model || model[0] == '\0') model = DEFAULT_PUBLICAI_MODEL;
    
    BString modelLine;
    modelLine.SetToFormat("  \"model\": \"%s\",\n", model);
    outPayload << modelLine;
    
    if (stream) {
        outPayload.Append("  \"stream\": true,\n");
    }
    
    outPayload.Append("  \"messages\": [");
    bool first = true;

    BMessage messagesMsg;
    if (config && config->FindMessage("messages", &messagesMsg) == B_OK) {
        BMessage msgItem;
        int32 i = 0;
        
        while (messagesMsg.FindMessage("msg", i, &msgItem) == B_OK || 
               messagesMsg.FindMessage(BString().SetToFormat("%d", i).String(), &msgItem) == B_OK) {
            
            const char* role = nullptr;
            const char* content = nullptr;
            msgItem.FindString("role", &role);
            
            if (msgItem.FindString("content", &content) != B_OK) {
                msgItem.FindString("text", &content);
            }

            if (role && content && content[0] != '\0') {
                if (!first) outPayload.Append(",");
                
                // PublicAI usa lo standard "assistant" invece di "model"
                BString mappedRole = (strcmp(role, "model") == 0) ? "assistant" : role;

                BString escapedContent(content);
                escapedContent.ReplaceAll("\\", "\\\\");
                escapedContent.ReplaceAll("\"", "\\\"");
                escapedContent.ReplaceAll("\n", "\\n");
                escapedContent.ReplaceAll("\r", "\\r");
                escapedContent.ReplaceAll("\t", "\\t");

                BString objectStr;
                objectStr.SetToFormat("{\"role\":\"%s\",\"content\":\"%s\"}", 
                                      mappedRole.String(), escapedContent.String());
                
                outPayload.Append(objectStr);
                first = false;
            }
            i++;
        }
    }

    if (first && currentPrompt && currentPrompt[0] != '\0') {
        BString escapedPrompt(currentPrompt);
        escapedPrompt.ReplaceAll("\\", "\\\\");
        escapedPrompt.ReplaceAll("\"", "\\\"");
        escapedPrompt.ReplaceAll("\n", "\\n");
        
        BString objectStr;
        objectStr.SetToFormat("{\"role\":\"user\",\"content\":\"%s\"}", escapedPrompt.String());
        outPayload.Append(objectStr);
    }

    outPayload.Append("]\n}");
}

// ============================================================================
// INTERFACCIA SDK PLUGIN C-LINKAGE
// ============================================================================

extern "C" ai_plugin_t ai_plugin_init(const BMessage* settingsMsg)
{
    PublicAIHandle* h = (PublicAIHandle*)malloc(sizeof(PublicAIHandle));
    if (!h) return nullptr;
    h->base_url = nullptr;
    
    if (settingsMsg) {
        const char* url = nullptr;
        // Estraiamo "base_url" direttamente dal messaggio nativo
        if (settingsMsg->FindString("base_url", &url) == B_OK && url[0] != '\0') {
            h->base_url = dupstr_or_null(url);
        }
    }
    return (ai_plugin_t)h;
}

extern "C" void ai_plugin_free(ai_plugin_t handle)
{
    PublicAIHandle* h = (PublicAIHandle*)handle;
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
    fprintf(stderr, "[PUBLICAI PLUGIN] === INIZIO generate_text_sync ===\n");
    PublicAIHandle* h = (PublicAIHandle*)handle;
    if (!config) {
        fprintf(stderr, "[PUBLICAI PLUGIN] ERRORE: il puntatore BMessage* config è NULL!\n");
        return B_ERROR;
    }
    
    fprintf(stderr, "[PUBLICAI PLUGIN] Ispezione BMessage di configurazione ricevuto:\n");
    config->PrintToStream();

    const char* apiKey = nullptr;
    config->FindString("api_key", &apiKey);
    
    if (!apiKey || apiKey[0] == '\0') {
        fprintf(stderr, "[PUBLICAI PLUGIN] ERRORE CRITICO: api_key non trovata!\n");
        snprintf(response_buf, response_len, "[publicai_plugin] error: no API key provided");
        return B_ERROR;
    }
    
    BString url = (h && h->base_url && h->base_url[0]) ? h->base_url : DEFAULT_PUBLICAI_URL;
    if (!url.EndsWith("/")) url.Append("/", 1);
    url.Append("chat/completions");
    fprintf(stderr, "[PUBLICAI PLUGIN] Target URL: %s\n", url.String());
    
    BString payload;
    BuildPayloadFromContext(config, prompt, payload, false);
    fprintf(stderr, "[PUBLICAI PLUGIN] Payload JSON generato:\n%s\n", payload.String());
    
    BMallocIO* out = new BMallocIO();
    SyncListener listener;
    BUrl bUrl(url.String(), true);
    
    BUrlRequest* req = BUrlProtocolRoster::MakeRequest(bUrl, out, &listener, NULL);
    if (!req) { 
        fprintf(stderr, "[PUBLICAI PLUGIN] ERRORE: creazione BUrlRequest fallita.\n");
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
        headers.AddHeader("User-Agent", PUBLICAI_USER_AGENT);
        
        BString authHeader;
        authHeader.SetToFormat("Bearer %s", apiKey);
        headers.AddHeader("Authorization", authHeader.String());
        http->SetHeaders(headers);
    }

    fprintf(stderr, "[PUBLICAI PLUGIN] Avvio richiesta HTTP...\n");
    thread_id thread = req->Run();
    status_t rc = B_ERROR;
    if (thread >= 0) {
        wait_for_thread(thread, &rc);
    } else {
        rc = thread;
    }
    fprintf(stderr, "[PUBLICAI PLUGIN] Richiesta conclusa. Thread rc: %d\n", (int)rc);

    const void* buf = out->Buffer();
    size_t len = out->BufferLength();
    fprintf(stderr, "[PUBLICAI PLUGIN] Buffer di risposta: %d byte ricevuti.\n", (int)len);

    int32 statusCode = 0;
    if (http) {
        const BHttpResult* httpResult = dynamic_cast<const BHttpResult*>(&(http->Result()));
        if (httpResult != nullptr) {
            statusCode = httpResult->StatusCode();
        }
    }
    fprintf(stderr, "[PUBLICAI PLUGIN] HTTP Status Code: %d\n", (int)statusCode);
    
    if (!buf || len == 0 || statusCode != 200) {
        if (buf && len > 0) {
            fprintf(stderr, "[PUBLICAI PLUGIN] Dettaglio errore server:\n%.*s\n", (int)len, (const char*)buf);
        }
        snprintf(response_buf, response_len, "{\"error\":\"http error or empty response\"}"); 
        delete req; delete out; 
        return B_ERROR; 
    }
    
    bool extractedSuccessfully = false;
    BString rawResponse((const char*)buf, len);

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
            fprintf(stderr, "[PUBLICAI PLUGIN] Testo estratto con successo.\n");
        }
    }

    if (!extractedSuccessfully) {
        fprintf(stderr, "[PUBLICAI PLUGIN] Parsing fallito, restituzione testo raw.\n");
        size_t copy_len = len < response_len - 1 ? len : response_len - 1;
        memcpy(response_buf, buf, copy_len);
        response_buf[copy_len] = '\0';
    }

    delete req; delete out;
    fprintf(stderr, "[PUBLICAI PLUGIN] === FINE generate_text_sync ===\n\n");
    return rc == B_OK ? 0 : -1;
}

static status_t publicai_stream_thread_func(void* data)
{
    fprintf(stderr, "[PUBLICAI STREAM WORKER] Thread avviato.\n");
    PublicAIAsyncArgs* args = (PublicAIAsyncArgs*)data;
    if (!args) return B_BAD_VALUE;

    if (!args->api_key || args->api_key[0] == '\0') {
        fprintf(stderr, "[PUBLICAI STREAM WORKER] ERRORE CRITICO: API key assente!\n");
        if (args->notify_path) {
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

    BString urlString;
    if (args->base_url && args->base_url[0] != '\0') {
        urlString << args->base_url;
        if (!urlString.EndsWith("/")) urlString.Append("/", 1);
        urlString.Append("chat/completions");
    } else {
        urlString = "https://api.publicai.co/v1/chat/completions";
    }

    BString payload;
    BuildPayloadFromContext(args->context_copy, nullptr, payload, true);
    fprintf(stderr, "[PUBLICAI STREAM WORKER] Payload Streaming generato:\n%s\n", payload.String());
    
    {
        StreamTarget streamTarget(args->notify_path);
        CompletionListener listener(args->notify_path);
        BUrl bUrl(urlString.String(), false);

        BUrlRequest* req = BUrlProtocolRoster::MakeRequest(bUrl, &streamTarget, &listener, NULL);
        if (req != nullptr) {
            BHttpRequest* http = dynamic_cast<BHttpRequest*>(req);
            if (http) {
                http->SetMethod(B_HTTP_POST);
                BHttpHeaders headers;
                headers.AddHeader("Content-Type", "application/json");
                headers.AddHeader("User-Agent", "HaikuAIserver/1.0"); 
                
                BString authHeader;
                authHeader << "Bearer " << args->api_key;
                headers.AddHeader("Authorization", authHeader.String());
                http->SetHeaders(headers);

                BMemoryIO* input = new BMemoryIO(payload.String(), payload.Length());
                http->AdoptInputData(input, payload.Length());
            }

            fprintf(stderr, "[PUBLICAI STREAM WORKER] Avvio streaming...\n");
            thread_id thread = req->Run();
            if (thread >= 0) {
                status_t rc;
                wait_for_thread(thread, &rc);
                fprintf(stderr, "[PUBLICAI STREAM WORKER] Streaming concluso (rc: %d).\n", (int)rc);
            }
            delete req;
        } else {
            fprintf(stderr, "[PUBLICAI STREAM WORKER] Errore creazione richiesta.\n");
            BFile streamFile(args->notify_path, B_WRITE_ONLY | B_CREATE_FILE);
            BString endMarker = "<<STREAM_END>>";
            streamFile.Write(endMarker.String(), endMarker.Length());
        }
    }

    fprintf(stderr, "[PUBLICAI STREAM WORKER] Cleanup di fine ciclo.\n");
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
    PublicAIHandle* h = (PublicAIHandle*)handle;
    if (!config) return B_ERROR;
    
    const char* apiKeyBuf = nullptr;
    const char* modelBuf = nullptr;
    const char* pathBuf = nullptr;
    
    config->FindString("api_key", &apiKeyBuf);
    config->FindString("model_name", &modelBuf);
    config->FindString("notify_path", &pathBuf);

    if (!pathBuf || pathBuf[0] == '\0') return B_ERROR;
    
    PublicAIAsyncArgs* args = (PublicAIAsyncArgs*)malloc(sizeof(PublicAIAsyncArgs));
    if (!args) return B_ERROR;
    
    args->api_key = dupstr_or_null(apiKeyBuf);
    args->model = dupstr_or_null(modelBuf && modelBuf[0] ? modelBuf : DEFAULT_PUBLICAI_MODEL);
    args->notify_path = dupstr_or_null(pathBuf);
    args->base_url = h ? dupstr_or_null(h->base_url) : nullptr;
    
    // Duplichiamo l'intero contesto nativo BMessage per passarlo al thread in sicurezza
    args->context_copy = new BMessage(*config);

    thread_id thread = spawn_thread(
        publicai_stream_thread_func,
        "publicai_stream_worker",
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
    const char* defaultFallback = "[\"swiss-ai/apertus-8b-instruct\"]";

    if (!config) {
        if (strlen(defaultFallback) + 1 > out_len) return B_ERROR;
        strcpy(out_buf, defaultFallback);
        return B_OK;
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

    BString urlString;
    if (baseUrl && baseUrl[0] != '\0') {
        urlString << baseUrl;
        if (!urlString.EndsWith("/")) urlString.Append("/", 1);
        urlString.Append("models");
    } else {
        urlString = "https://api.publicai.co/v1/models";
    }

    BMallocIO* out = new BMallocIO();
    SyncListener listener;
    BUrl bUrl(urlString.String(), false);

    BUrlRequest* req = BUrlProtocolRoster::MakeRequest(bUrl, out, &listener, NULL);
    if (!req) { delete out; return B_ERROR; }
    
    int32 statusCode = 0;
    BHttpRequest* http = dynamic_cast<BHttpRequest*>(req);
    if (http) {
        http->SetMethod(B_HTTP_GET);
        BHttpHeaders headers;
        headers.AddHeader("User-Agent", "HaikuAIserver/1.0"); 
        BString authHeader;
        authHeader.SetToFormat("Bearer %s", apiKey);
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
        int32 matchPos = jsonResponse.FindFirst("\"id\"", currentPos);
        if (matchPos == B_ERROR) break;

        matchPos += 4; 
        int32 valueStart = jsonResponse.FindFirst("\"", matchPos);
        if (valueStart == B_ERROR) break;
        
        valueStart += 1;
        int32 valueEnd = jsonResponse.FindFirst("\"", valueStart);
        if (valueEnd == B_ERROR) break;

        BString modelName;
        jsonResponse.CopyInto(modelName, valueStart, valueEnd - valueStart);
        modelName.Trim();

        if (!modelName.IsEmpty() && modelName.FindFirst("embed") == B_ERROR && modelName.FindFirst("rerank") == B_ERROR) {
            if (!first) {
                cleanJson.Append(", ");
            }
            cleanJson << "\"" << modelName << "\"";
            first = false;
        }
        currentPos = valueEnd + 1;
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
    return "PublicAIPlugin";
}

uint32 ai_plugin_get_capabilities() {
    return AI_CAP_STREAMING;
}
