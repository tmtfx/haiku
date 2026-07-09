// gemini_plugin.cpp
// Plugin per il servizio Google Gemini AI su Haiku - Versione Stateless Concorrente.
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
#include <Message.h>
#include <Path.h>

#include <Json.h>
#include <HttpResult.h>



#include <UrlProtocolRoster.h>
#include <UrlRequest.h>
#include <HttpRequest.h>
#include <UrlProtocolListener.h>
#include <DataIO.h>


using namespace BPrivate::Network;

/* gestione intelligente del testo cumulativo */
// ============================================================================
// DIFFERENZE RISPETTO ALLA PRIMA VERSIONE (MOTIVAZIONI ARCHITETTURALI):
// ============================================================================
// 1. GESTIONE DELLO STREAMING CUMULATIVO (ANTI-DUPLICAZIONE DI RETE)
//    La prima versione assumeva che ogni blocco JSON contenesse un delta puro 
//    (un singolo token alla volta). L'API di Gemini, invece, reinvia spesso 
//    nei chunk successivi tutto il testo generato dall'inizio fino a quel momento.
//    La prima versione avrebbe quindi scritto sul file FIFO duplicati incrementali 
//    del tipo: "Ciao", "Ciao come", "Ciao come va".
//
//    Questa versione introduce 'fLastToken' per tenere traccia della timeline 
//    globale del testo già inviato al client. Tramite il controllo 'StartsWith',
//    calcola matematicamente il delta reale (la differenza) e scrive sul file 
//    solo ed esclusivamente i caratteri nuovi generati nell'ultimo giro.
//
// 2. FILTRO SUI PACCHETTI DI RETE CONCORRENTI / VUOTI
//    Se la scheda di rete accumula frame o se il server invia due chunk strutturati 
//    identici prima di avanzare con la generazione, il controllo sulla lunghezza 
//    (token.Length() > fLastToken.Length()) scarta automaticamente i raddoppi 
//    di trasmissione senza sprecare scritture I/O sul file.
//
// 3. LOG DI EMERGENZA (DEBUGBING STRUTTURATO)
//    Include un check di diagnostica iniziale che intercetta i pacchetti in cui 
//    la chiave \"text\": \" è assente (es. in caso di risposte d'errore HTTP del 
//    tipo 429 'Resource Exhausted' per limiti di quota superati o 400 Bad Request),
//    evitando blocchi silenziosi del thread.
// ============================================================================
/*
class StreamTarget : public BDataIO {
public:
    StreamTarget(const char* notifyPath) {
        fFile.SetTo(notifyPath, B_WRITE_ONLY | B_CREATE_FILE | B_OPEN_AT_END);
        fBuffer.SetTo("");
        fLastToken.SetTo("");
    }
virtual ssize_t Write(const void* buffer, size_t size) override {
    if (fFile.InitCheck() != B_OK || size == 0) return size;

    fBuffer.Append((const char*)buffer, size);

    // LOG DI EMERGENZA: Vediamo cosa sta arrivando davvero dal server
    if (fBuffer.FindFirst("\"text\": \"") == B_ERROR) {
        fprintf(stderr, "[DEBUG SERVER] Ricevuti %d byte, ma niente 'text'. Contenuto: %s\n", 
            (int)size, fBuffer.String());
    }

    int32 currentPos = 0;
    while (true) {
        int32 matchPos = fBuffer.FindFirst("\"text\": \"", currentPos);
        if (matchPos == B_ERROR) break;
        
        matchPos += 9;
        int32 endContent = fBuffer.FindFirst("\"", matchPos);
        
        while (endContent != B_ERROR && fBuffer.ByteAt(endContent - 1) == '\\') {
            endContent = fBuffer.FindFirst("\"", endContent + 1);
        }

        if (endContent == B_ERROR) break;

        BString token;
        fBuffer.CopyInto(token, matchPos, endContent - matchPos);
        
        token.ReplaceAll("\\n", "\n");
        token.ReplaceAll("\\t", "\t");
        token.ReplaceAll("\\\"", "\"");
        token.ReplaceAll("\\\\", "\\");

        if (token.Length() > fLastToken.Length() && token.StartsWith(fLastToken)) {
            BString diff;
            token.CopyInto(diff, fLastToken.Length(), token.Length() - fLastToken.Length());
            fFile.Write(diff.String(), diff.Length());
            fLastToken = token;
        } else if (!token.IsEmpty() && fLastToken.IsEmpty()) {
            fFile.Write(token.String(), token.Length());
            fLastToken = token;
        }
        
        currentPos = endContent + 1;
    }

    if (currentPos > 0) {
        fBuffer.Remove(0, currentPos);
    }

    return size;
}
private:
    BFile   fFile;
    BString fBuffer; // Memorizza i residui dei chunk parziali
    BString fLastToken;
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

// L'handle ora serve solo per configurazioni globali persistenti (es. base_url di sistema o proxy)
struct GeminiHandle {
    char* base_url;
};

// Argomenti isolati passati al thread worker per lo streaming asincrono
struct GeminiAsyncArgs {
    char* api_key;
    char* model;
    char* prompt;
    char* notify_path;
    char* base_url;
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
        return false; // Blocca la richiesta se il certificato SSL fallisce
    }
};

extern "C" ai_plugin_t ai_plugin_init(const char* config_json)
{
    GeminiHandle* h = (GeminiHandle*)malloc(sizeof(GeminiHandle));
    if (!h) return nullptr;
    h->base_url = nullptr;
    
    if (config_json) {
        char buf[512];
        extract_json_field(config_json, "base_url", buf, sizeof(buf));
        if (buf[0]) h->base_url = dupstr_or_null(buf);
    }
    return (ai_plugin_t)h;
}

extern "C" void ai_plugin_free(ai_plugin_t handle)
{
    GeminiHandle* h = (GeminiHandle*)handle;
    if (!h) return;
    if (h->base_url) free(h->base_url);
    free(h);
}

extern "C" status_t ai_plugin_generate_text_sync(ai_plugin_t handle,
                                             const char* prompt,
                                             char* response_buf,
                                             size_t response_len,
                                             const char* config_json)
{
    GeminiHandle* h = (GeminiHandle*)handle;
    if (!prompt || !config_json) return B_ERROR;
    
    // Estrazione atomica dei parametri della sessione corrente
    char apiKeyBuf[512];
    char modelBuf[256];
    extract_json_field(config_json, "api_key", apiKeyBuf, sizeof(apiKeyBuf));
    extract_json_field(config_json, "model", modelBuf, sizeof(modelBuf));

    const char* model = modelBuf[0] ? modelBuf : "gemini-2.5-flash";
    
    if (apiKeyBuf[0] == '\0') {
        const char* err = "[gemini_plugin] error: no API key provided in session config";
        if (strlen(err) + 1 > response_len) return B_ERROR;
        strcpy(response_buf, err);
        return B_ERROR;
    }
    
    // Costruzione URL
    BString url;
    if (h && h->base_url && h->base_url[0]) {
        url << h->base_url;
    } else {
        url.SetToFormat("https://generativelanguage.googleapis.com/v1beta/models/%s:generateContent", model);
    }
    url << "?key=" << apiKeyBuf;
    
    // Costruzione payload JSON
    BString payload;
    payload.Append("{\"contents\":[{\"parts\":[{\"text\":\"");
    for (const char* p = prompt; *p; ++p) {
        if (*p == '"' || *p == '\\') payload.Append('\\', 1);
        else if (*p == '\n') payload.Append("\\n", 2);
        else if (*p == '\r') payload.Append("\\r", 2);
        else if (*p == '\t') payload.Append("\\t", 2);
        else payload.Append(*p, 1);
    }
    payload.Append("\"}]}]}");
    
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
            BMessage candidates, candidateZero, content, parts, partZero;
            const char* extractedText = nullptr;

            if (parsedJson.FindMessage("candidates", &candidates) == B_OK
                && candidates.FindMessage("0", &candidateZero) == B_OK
                && candidateZero.FindMessage("content", &content) == B_OK
                && content.FindMessage("parts", &parts) == B_OK
                && parts.FindMessage("0", &partZero) == B_OK
                && partZero.FindString("text", &extractedText) == B_OK) {
                
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

static status_t gemini_stream_thread_func(void* data)
{
    GeminiAsyncArgs* args = (GeminiAsyncArgs*)data;
    if (!args) return B_BAD_VALUE;

    // 1. Costruzione URL nativo Gemini per lo streaming
    BString url;
    if (args->base_url && args->base_url[0] != '\0') {
        url << args->base_url;
    } else {
        url.SetToFormat("https://generativelanguage.googleapis.com/v1beta/models/%s:streamGenerateContent", args->model);
    }
    url << "?key=" << args->api_key;

    // 2. Costruzione payload JSON nel formato corretto di Google (con escaping del prompt)
    BString escapedPrompt;
    for (const char* p = args->prompt; *p; ++p) {
        if (*p == '"' || *p == '\\') escapedPrompt.Append('\\', 1);
        else if (*p == '\n') escapedPrompt.Append("\\n", 2);
        else if (*p == '\r') escapedPrompt.Append("\\r", 2);
        else if (*p == '\t') escapedPrompt.Append("\\t", 2);
        else escapedPrompt.Append(*p, 1);
    }
    
    BString payload;
    payload << "{\"contents\":[{\"parts\":[{\"text\":\"" << escapedPrompt << "\"}]}]}";

    // Istanziamo i target reali di Haiku
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
    }

    delete req;

cleanup:
    if (args->api_key) free(args->api_key);
    if (args->model) free(args->model);
    if (args->prompt) free(args->prompt);
    if (args->notify_path) free(args->notify_path);
    if (args->base_url) free(args->base_url);
    free(args);

    return B_OK;
}

extern "C" status_t ai_plugin_generate_text_async(ai_plugin_t handle, const char* prompt, const char* config_json)
{
    GeminiHandle* h = (GeminiHandle*)handle;
    if (!prompt || !config_json) return B_ERROR;
    
    // Estraiamo i campi al volo dal JSON della sessione corrente
    char apiKeyBuf[512];
    char modelBuf[256];
    char pathBuf[512];
    
    extract_json_field(config_json, "api_key", apiKeyBuf, sizeof(apiKeyBuf));
    extract_json_field(config_json, "model", modelBuf, sizeof(modelBuf));
    extract_json_field(config_json, "notify_path", pathBuf, sizeof(pathBuf));

    if (pathBuf[0] == '\0') {
        fprintf(stderr, "[GEMINI_PLUGIN] Errore: notify_path mancante nella sessione.\n");
        return B_ERROR;
    }
    
    // Allociamo gli argomenti isolati per il thread (nessun lock globale necessario!)
    GeminiAsyncArgs* args = (GeminiAsyncArgs*)malloc(sizeof(GeminiAsyncArgs));
    if (!args) return B_ERROR;
    
    args->api_key = dupstr_or_null(apiKeyBuf);
    args->model = dupstr_or_null(modelBuf[0] ? modelBuf : "gemini-2.5-flash");
    args->prompt = dupstr_or_null(prompt);
    args->notify_path = dupstr_or_null(pathBuf);
    args->base_url = h ? dupstr_or_null(h->base_url) : nullptr;

    thread_id thread = spawn_thread(
        gemini_stream_thread_func,
        "gemini_stream_worker",
        B_NORMAL_PRIORITY,
        args
    );

    if (thread < B_OK) {
        if (args->api_key) free(args->api_key);
        if (args->model) free(args->model);
        if (args->prompt) free(args->prompt);
        if (args->notify_path) free(args->notify_path);
        if (args->base_url) free(args->base_url);
        free(args);
        return B_ERROR;
    }

    resume_thread(thread);
    return B_OK; // Torna immediatamente il controllo all'ai_server
}

extern "C" status_t ai_plugin_list_models(const char* config_json, char* out_buf, size_t out_len)
{
    const char* defaultFallback = "[\"gemini-2.5-flash\", \"gemini-1.5-pro\", \"gemini-pro\"]";
    
    char apiKey[128] = {0};
    char baseUrl[256] = {0};

    // Estraiamo i campi usando la tua funzione definita in AIPlugin.h
    extract_json_field(config_json, "api_key", apiKey, sizeof(apiKey));
    extract_json_field(config_json, "base_url", baseUrl, sizeof(baseUrl));

    // Se non c'è la chiave, inutile fare la richiesta HTTP, andiamo di fallback immediato
    if (apiKey[0] == '\0') {
        if (strlen(defaultFallback) + 1 > out_len) return B_ERROR;
        strcpy(out_buf, defaultFallback);
        return B_OK;
    }

    // Configurazione dell'URL (se base_url è vuoto usiamo l'endpoint di default)
    BString url = (baseUrl[0] != '\0') ? baseUrl : "https://generativelanguage.googleapis.com/v1beta";
    if (!url.EndsWith("/")) url.Append("/", 1);
    url.Append("models");
    url << "?key=" << apiKey;

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
        const char* fallback = "[\"gemini-2.5-flash\"]";
        if (strlen(fallback) + 1 > out_len) return B_ERROR;
        strcpy(out_buf, fallback);
        return B_OK;
    }

    // --- PARSER E FILTRO DEI MODELLI GEMINI ---
    // Il server risponde con "models/gemini-2.5-flash". 
    // Dobbiamo estrarre la parte dopo "models/" e impacchettarla in ["...", "..."]
    
    BString jsonResponse((const char*)out->Buffer(), out->BufferLength());
    BString cleanJson("[");
    int32 currentPos = 0;
    bool first = true;

    while (true) {
        int32 matchPos = jsonResponse.FindFirst("\"name\": \"models/", currentPos);
        if (matchPos == B_ERROR) break;

        matchPos += 16; 
        
        int32 endPos = jsonResponse.FindFirst("\"", matchPos);
        if (endPos == B_ERROR) break;

        BString modelName;
        jsonResponse.CopyInto(modelName, matchPos, endPos - matchPos);

        // Uso corretto di FindFirst per Haiku (B_ERROR significa "non trovato")
        bool isEmbedding = (modelName.FindFirst("embedding") != B_ERROR);
        bool isAqa = (modelName.FindFirst("aqa") != B_ERROR);

        if (!isEmbedding && !isAqa) {
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
        cleanJson = "[\"gemini-2.5-flash\"]";
    }

    // Risoluzione del warning: castiamo Length() a size_t per il confronto
    size_t jsonLength = (size_t)cleanJson.Length();
    size_t copy_len = jsonLength < out_len - 1 ? jsonLength : out_len - 1;
    
    memcpy(out_buf, cleanJson.String(), copy_len);
    out_buf[copy_len] = '\0';

    delete req; delete out;
    return B_OK;
}*/
class StreamTarget : public BDataIO {
public:
    StreamTarget(const char* notifyPath) {
        fFile.SetTo(notifyPath, B_WRITE_ONLY | B_CREATE_FILE | B_OPEN_AT_END);
        fBuffer.SetTo("");
        fLastToken.SetTo("");
    }

    virtual ssize_t Write(const void* buffer, size_t size) override {
        if (fFile.InitCheck() != B_OK || size == 0) return size;

        fBuffer.Append((const char*)buffer, size);

        int32 currentPos = 0;
        while (true) {
            int32 matchPos = fBuffer.FindFirst("\"text\": \"", currentPos);
            if (matchPos == B_ERROR) break;
            
            matchPos += 9;
            int32 endContent = fBuffer.FindFirst("\"", matchPos);
            
            while (endContent != B_ERROR && fBuffer.ByteAt(endContent - 1) == '\\') {
                endContent = fBuffer.FindFirst("\"", endContent + 1);
            }

            if (endContent == B_ERROR) break;

            BString token;
            fBuffer.CopyInto(token, matchPos, endContent - matchPos);
            
            token.ReplaceAll("\\n", "\n");
            token.ReplaceAll("\\t", "\t");
            token.ReplaceAll("\\\"", "\"");
            token.ReplaceAll("\\\\", "\\");

            if (token.Length() > fLastToken.Length() && token.StartsWith(fLastToken)) {
                BString diff;
                token.CopyInto(diff, fLastToken.Length(), token.Length() - fLastToken.Length());
                fFile.Write(diff.String(), diff.Length());
                fLastToken = token;
            } else if (!token.IsEmpty() && fLastToken.IsEmpty()) {
                fFile.Write(token.String(), token.Length());
                fLastToken = token;
            }
            
            currentPos = endContent + 1;
        }

        if (currentPos > 0) {
            fBuffer.Remove(0, currentPos);
        }

        return size;
    }
private:
    BFile   fFile;
    BString fBuffer;
    BString fLastToken;
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

struct GeminiHandle {
    char* base_url;
};

struct GeminiAsyncArgs {
    char* api_key;
    char* model;
    char* notify_path;
    char* base_url;
    BMessage* context_copy; // Copia del contesto nativo della chat
};

static char* dupstr_or_null(const char* s) {
    if (!s) return nullptr;
    size_t n = strlen(s) + 1;
    char* p = (char*)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

// Funzione helper per iniettare l'intero storico dei messaggi salvato nel BMessage nel JSON di Gemini
void BuildPayloadFromContext(const BMessage* config, const char* currentPrompt, BString& outPayload)
{
    outPayload.SetTo("{\"contents\":[");
    bool first = true;

    BMessage messagesMsg;
    if (config && config->FindMessage("messages", &messagesMsg) == B_OK) {
        BMessage msgItem;
        int32 i = 0;
        
        // Controlliamo sia l'indice nativo dell'array (msg[0]) sia la stringa "0"
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
                
                BString geminiRole = (strcmp(role, "assistant") == 0) ? "model" : "user";

                // Gestiamo l'escape dei caratteri JSON speciali (es. virgolette o newline) nel testo
                BString escapedContent(content);
                escapedContent.ReplaceAll("\\", "\\\\");
                escapedContent.ReplaceAll("\"", "\\\"");
                escapedContent.ReplaceAll("\n", "\\n");
                escapedContent.ReplaceAll("\r", "\\r");
                escapedContent.ReplaceAll("\t", "\\t");

                BString objectStr;
                objectStr.SetToFormat("{\"role\":\"%s\",\"parts\":[{\"text\":\"%s\"}]}", 
                                      geminiRole.String(), escapedContent.String());
                
                outPayload.Append(objectStr);
                first = false;
            }
            i++;
        }
    }

    // Se lo storico non ha estratto nulla, o se vogliamo assicurarci di inviare il prompt corrente
    if (first && currentPrompt && currentPrompt[0] != '\0') {
        BString escapedPrompt(currentPrompt);
        escapedPrompt.ReplaceAll("\\", "\\\\");
        escapedPrompt.ReplaceAll("\"", "\\\"");
        escapedPrompt.ReplaceAll("\n", "\\n");
        
        BString objectStr;
        objectStr.SetToFormat("{\"role\":\"user\",\"parts\":[{\"text\":\"%s\"}]}", escapedPrompt.String());
        outPayload.Append(objectStr);
    }

    // Chiusura matematica dell'array e dell'oggetto radice
    outPayload.Append("]}");
}

class SyncListener : public BUrlProtocolListener {
public:
    SyncListener() {}
    virtual ~SyncListener() {}
    bool CertificateVerificationFailed(BUrlRequest* request, BCertificate& certificate, const char* message) override {
        return false; 
    }
};

extern "C" ai_plugin_t ai_plugin_init(const BMessage* config)
{
    GeminiHandle* h = (GeminiHandle*)malloc(sizeof(GeminiHandle));
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
    GeminiHandle* h = (GeminiHandle*)handle;
    if (!h) return;
    if (h->base_url) free(h->base_url);
    free(h);
}

extern "C" status_t ai_plugin_update_config(ai_plugin_t handle, const BMessage* config)
{
    GeminiHandle* h = (GeminiHandle*)handle;
    if (!h || !config) return B_BAD_VALUE;
    
    const char* url = nullptr;
    if (config->FindString("base_url", &url) == B_OK) {
        if (h->base_url) free(h->base_url);
        h->base_url = dupstr_or_null(url);
    }
    return B_OK;
}

extern "C" status_t ai_plugin_generate_text_sync(ai_plugin_t handle,
                                             const char* prompt,
                                             char* response_buf,
                                             size_t response_len,
                                             BMessage* config)
{
	fprintf(stderr, "[GEMINI PLUGIN] === INIZIO generate_text_sync ===\n");
    GeminiHandle* h = (GeminiHandle*)handle;
    if (!config) {
        fprintf(stderr, "[GEMINI PLUGIN] ERRORE: il puntatore BMessage* config è NULL!\n");
        return B_ERROR;
    }
    fprintf(stderr, "[GEMINI PLUGIN] Ispezione BMessage di configurazione ricevuto:\n");
    config->PrintToStream();
    
    const char* apiKey = nullptr;
    const char* model = nullptr;
    config->FindString("api_key", &apiKey);
    config->FindString("model_name", &model);

    if (!model || model[0] == '\0') model = "gemini-2.5-flash";
    
    if (!apiKey || apiKey[0] == '\0') {
        fprintf(stderr, "[GEMINI PLUGIN] ERRORE CRITICO: api_key non trovata o vuota nel BMessage!\n");
        snprintf(response_buf, response_len, "[gemini_plugin] error: no API key provided");
        return B_ERROR;
    }
    
    BString url;
    if (h && h->base_url && h->base_url[0]) {
        url << h->base_url;
    } else {
        url.SetToFormat("https://generativelanguage.googleapis.com/v1beta/models/%s:generateContent", model);
    }
    url << "?key=" << apiKey;
    fprintf(stderr, "[GEMINI PLUGIN] Target URL (chiave oscurata): https://generativelanguage.googleapis.com/...:generateContent\n");
    // Compiliamo il payload iniettando lo storico memorizzato nel BMessage
    BString payload;
    BuildPayloadFromContext(config, prompt, payload);
    fprintf(stderr, "[GEMINI PLUGIN] Payload JSON generato:\n%s\n", payload.String());
    
    BMallocIO* out = new BMallocIO();
    SyncListener listener;
    BUrl bUrl(url.String(), true);
    
    BUrlRequest* req = BUrlProtocolRoster::MakeRequest(bUrl, out, &listener, NULL);
    if (!req) {
    	fprintf(stderr, "[GEMINI PLUGIN] ERRORE: BUrlProtocolRoster::MakeRequest ha fallito la creazione del client di rete!\n");
        delete out; 
        snprintf(response_buf, response_len, "{\"error\":\"request failed\"}"); // <--- Serve????
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
        http->SetHeaders(headers);
    }

    fprintf(stderr, "[GEMINI PLUGIN] Avvio richiesta HTTP di rete sincrona...\n");
    thread_id thread = req->Run();
    status_t rc = B_ERROR;
    if (thread >= 0) {
        wait_for_thread(thread, &rc);
    } else {
        rc = thread;
    }
    fprintf(stderr, "[GEMINI PLUGIN] Chiamata di rete conclusa. Thread return code: %d\n", (int)rc);

    const void* buf = out->Buffer();
    size_t len = out->BufferLength();
    if (!buf || len == 0) { 
        snprintf(response_buf, response_len, "{\"error\":\"empty response\"}"); 
        delete req; delete out; 
        return B_ERROR; 
    }
    fprintf(stderr, "[GEMINI PLUGIN] Buffer di risposta: %d byte ricevuti.\n", (int)len);
    
    
    int32 statusCode = 0;
    if (http) {
        const BHttpResult* httpResult = dynamic_cast<const BHttpResult*>(&(http->Result()));
        if (httpResult != nullptr) {
            statusCode = httpResult->StatusCode();
        }
    }
    fprintf(stderr, "[GEMINI PLUGIN] HTTP Status Code Server Google: %d\n", (int)statusCode);
    
    if (!buf || len == 0 || statusCode != 200) {
        if (buf && len > 0) {
            fprintf(stderr, "[GEMINI PLUGIN] Dettaglio errore server remoto:\n%.*s\n", (int)len, (const char*)buf);
        }
        snprintf(response_buf, response_len, "{\"error\":\"http error or empty response\"}"); 
        delete req; delete out; 
        return B_ERROR; 
    }
    
    bool extractedSuccessfully = false;
    BString rawResponse((const char*)buf, len);

    if (statusCode == 200) {
        BMessage parsedJson;
        if (BJson::Parse(rawResponse.String(), parsedJson) == B_OK) {
            BMessage candidates, candidateZero, content, parts, partZero;
            const char* extractedText = nullptr;

            if (parsedJson.FindMessage("candidates", &candidates) == B_OK
                && candidates.FindMessage("0", &candidateZero) == B_OK
                && candidateZero.FindMessage("content", &content) == B_OK
                && content.FindMessage("parts", &parts) == B_OK
                && parts.FindMessage("0", &partZero) == B_OK
                && partZero.FindString("text", &extractedText) == B_OK) {
                
                size_t textLen = strlen(extractedText);
                size_t copy_len = textLen < response_len - 1 ? textLen : response_len - 1;
                memcpy(response_buf, extractedText, copy_len);
                response_buf[copy_len] = '\0';
                extractedSuccessfully = true;
                fprintf(stderr, "[GEMINI PLUGIN] Testo estratto con successo dal JSON.\n");
            }
        }
    }

    if (!extractedSuccessfully) {
        fprintf(stderr, "[GEMINI PLUGIN] ATTENZIONE: Parsing strutturato fallito, restituzione testo raw.\n");
        size_t copy_len = len < response_len - 1 ? len : response_len - 1;
        memcpy(response_buf, buf, copy_len);
        response_buf[copy_len] = '\0';
    }

    delete req; delete out;
    fprintf(stderr, "[GEMINI PLUGIN] === FINE generate_text_sync ===\n\n");
    return rc == B_OK ? 0 : -1;
}
/*
static status_t gemini_stream_thread_func(void* data)
{
	fprintf(stderr, "[GEMINI STREAM WORKER] Thread avviato.\n");
    GeminiAsyncArgs* args = (GeminiAsyncArgs*)data;
    if (!args) return B_BAD_VALUE;

    
    fprintf(stderr, "[GEMINI STREAM WORKER] Parametri: Model='%s', Path='%s'\n", args->model, args->notify_path);
    if (!args->api_key || args->api_key[0] == '\0') {
        fprintf(stderr, "[GEMINI STREAM WORKER] ERRORE CRITICO: API key assente nell'argomento del thread!\n");
        // Scriviamo il terminatore per sbloccare la FIFO del server ed evitare lock infiniti
        BFile streamFile(args->notify_path, B_WRITE_ONLY | B_CREATE_FILE);
        BString endMarker = "<<STREAM_END>>";
        streamFile.Write(endMarker.String(), endMarker.Length());
        goto cleanup;
    }
    
    
    BString url;
    if (args->base_url && args->base_url[0] != '\0') {
        url << args->base_url;
    } else {
        url.SetToFormat("https://generativelanguage.googleapis.com/v1beta/models/%s:streamGenerateContent", args->model);
    }
    url << "?key=" << args->api_key;

    // Generiamo l'intero payload partendo dallo storico reale clonato nel thread worker
    BString payload;
    BuildPayloadFromContext(args->context_copy, nullptr, payload);
    fprintf(stderr, "[GEMINI STREAM WORKER] Payload Streaming generato:\n%s\n", payload.String());

    StreamTarget streamTarget(args->notify_path);
    CompletionListener listener(args->notify_path);
    BUrl bUrl(url.String(), true);

    BUrlRequest* req = BUrlProtocolRoster::MakeRequest(bUrl, &streamTarget, &listener, NULL);
    if (!req) {
    	fprintf(stderr, "[GEMINI STREAM WORKER] Errore di creazione della BUrlRequest di streaming.\n");
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
            http->SetHeaders(headers);

            BMallocIO* input = new BMallocIO();
            input->WriteExactly(payload.String(), payload.Length());
            http->AdoptInputData(input, payload.Length());
        }
        fprintf(stderr, "[GEMINI STREAM WORKER] Esecuzione loop di streaming di rete...\n");

        thread_id thread = req->Run();
        if (thread >= 0) {
            status_t rc;
            wait_for_thread(thread, &rc);
            fprintf(stderr, "[GEMINI STREAM WORKER] Streaming terminato (rc: %d).\n", (int)rc);
        }
    }

    delete req;

cleanup:
	fprintf(stderr, "[GEMINI STREAM WORKER] Pulizia e chiusura thread worker.\n");
    if (args->api_key) free(args->api_key);
    if (args->model) free(args->model);
    if (args->notify_path) free(args->notify_path);
    if (args->base_url) free(args->base_url);
    if (args->context_copy) delete args->context_copy; // Distrugge la copia isolata del BMessage
    free(args);

    return B_OK;
}*/
static status_t gemini_stream_thread_func(void* data)
{
    fprintf(stderr, "[GEMINI STREAM WORKER] Thread avviato.\n");
    GeminiAsyncArgs* args = (GeminiAsyncArgs*)data;
    
    // 1. Controllo di sicurezza immediato prima di allocare oggetti nello stack
    if (!args) return B_BAD_VALUE;

    if (!args->api_key || args->api_key[0] == '\0') {
        fprintf(stderr, "[GEMINI STREAM WORKER] ERRORE CRITICO: API key assente!\n");
        if (args->notify_path) {
            BFile streamFile(args->notify_path, B_WRITE_ONLY | B_CREATE_FILE);
            BString endMarker = "<<STREAM_END>>";
            streamFile.Write(endMarker.String(), endMarker.Length());
        }
        // Liberiamo la memoria ed usciamo subito in sicurezza senza goto
        if (args->api_key) free(args->api_key);
        if (args->model) free(args->model);
        if (args->notify_path) free(args->notify_path);
        if (args->base_url) free(args->base_url);
        if (args->context_copy) delete args->context_copy;
        free(args);
        return B_ERROR;
    }

    // 2. Ora possiamo dichiarare in sicurezza le variabili nello stack
    BString url;
    if (args->base_url && args->base_url[0] != '\0') {
        url << args->base_url;
    } else {
        url.SetToFormat("https://generativelanguage.googleapis.com/v1beta/models/%s:streamGenerateContent", args->model);
    }
    url << "?key=" << args->api_key;

    BString payload;
    BuildPayloadFromContext(args->context_copy, nullptr, payload);
    fprintf(stderr, "[GEMINI STREAM WORKER] Payload Streaming generato:\n%s\n", payload.String());

    {
        StreamTarget streamTarget(args->notify_path);
        CompletionListener listener(args->notify_path);
        BUrl bUrl(url.String(), true);

        BUrlRequest* req = BUrlProtocolRoster::MakeRequest(bUrl, &streamTarget, &listener, NULL);
        if (req != nullptr) {
            BHttpRequest* http = dynamic_cast<BHttpRequest*>(req);
            if (http) {
                http->SetMethod(B_HTTP_POST);
                BHttpHeaders headers;
                headers.AddHeader("Content-Type", "application/json");
                http->SetHeaders(headers);

                BMallocIO* input = new BMallocIO();
                input->WriteExactly(payload.String(), payload.Length());
                http->AdoptInputData(input, payload.Length());
            }

            fprintf(stderr, "[GEMINI STREAM WORKER] Esecuzione loop di streaming di rete...\n");
            thread_id thread = req->Run();
            if (thread >= 0) {
                status_t rc;
                wait_for_thread(thread, &rc);
                fprintf(stderr, "[GEMINI STREAM WORKER] Streaming terminato (rc: %d).\n", (int)rc);
            }
            delete req;
        } else {
            fprintf(stderr, "[GEMINI STREAM WORKER] Errore di creazione della BUrlRequest di streaming.\n");
            BFile streamFile(args->notify_path, B_WRITE_ONLY | B_CREATE_FILE);
            BString endMarker = "<<STREAM_END>>";
            streamFile.Write(endMarker.String(), endMarker.Length());
        }
    }

    // 3. Cleanup standard all'uscita nominale
    fprintf(stderr, "[GEMINI STREAM WORKER] Pulizia e chiusura thread worker.\n");
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
    GeminiHandle* h = (GeminiHandle*)handle;
    if (!config) return B_ERROR;
    
    const char* apiKey = nullptr;
    const char* model = nullptr;
    const char* notifyPath = nullptr;
    
    config->FindString("api_key", &apiKey);
    config->FindString("model_name", &model);
    config->FindString("notify_path", &notifyPath);

    if (!notifyPath || notifyPath[0] == '\0') {
        fprintf(stderr, "[GEMINI_PLUGIN] Errore: notify_path mancante.\n");
        return B_ERROR;
    }
    
    GeminiAsyncArgs* args = (GeminiAsyncArgs*)malloc(sizeof(GeminiAsyncArgs));
    if (!args) return B_ERROR;
    
    args->api_key = dupstr_or_null(apiKey);
    args->model = dupstr_or_null(model && model[0] ? model : "gemini-2.5-flash");
    args->notify_path = dupstr_or_null(notifyPath);
    args->base_url = h ? dupstr_or_null(h->base_url) : nullptr;
    
    // Cloniamo il BMessage di contesto per renderlo thread-safe (evitando corruzioni di memoria asincrone)
    args->context_copy = new BMessage(*config);

    thread_id thread = spawn_thread(
        gemini_stream_thread_func,
        "gemini_stream_worker",
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
    const char* defaultFallback = "[\"gemini-2.5-flash\", \"gemini-1.5-pro\", \"gemini-pro\"]";
    
    const char* apiKey = nullptr;
    const char* baseUrl = nullptr;
    if (config) {
        config->FindString("api_key", &apiKey);
        config->FindString("base_url", &baseUrl);
    }

    if (!apiKey || apiKey[0] == '\0') {
        if (strlen(defaultFallback) + 1 > out_len) return B_ERROR;
        strcpy(out_buf, defaultFallback);
        return B_OK;
    }

    BString url = (baseUrl && baseUrl[0] != '\0') ? baseUrl : "https://generativelanguage.googleapis.com/v1beta";
    if (!url.EndsWith("/")) url.Append("/", 1);
    url.Append("models");
    url << "?key=" << apiKey;

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
        const char* fallback = "[\"gemini-2.5-flash\"]";
        if (strlen(fallback) + 1 > out_len) return B_ERROR;
        strcpy(out_buf, fallback);
        return B_OK;
    }
    
    BString jsonResponse((const char*)out->Buffer(), out->BufferLength());
    BString cleanJson("[");
    int32 currentPos = 0;
    bool first = true;

    while (true) {
        int32 matchPos = jsonResponse.FindFirst("\"name\": \"models/", currentPos);
        if (matchPos == B_ERROR) break;

        matchPos += 16; 
        
        int32 endPos = jsonResponse.FindFirst("\"", matchPos);
        if (endPos == B_ERROR) break;

        BString modelName;
        jsonResponse.CopyInto(modelName, matchPos, endPos - matchPos);

        bool isEmbedding = (modelName.FindFirst("embedding") != B_ERROR);
        bool isAqa = (modelName.FindFirst("aqa") != B_ERROR);

        if (!isEmbedding && !isAqa) {
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
        cleanJson = "[\"gemini-2.5-flash\"]";
    }

    size_t jsonLength = (size_t)cleanJson.Length();
    size_t copy_len = jsonLength < out_len - 1 ? jsonLength : out_len - 1;
    
    memcpy(out_buf, cleanJson.String(), copy_len);
    out_buf[copy_len] = '\0';

    delete req; delete out;
    return B_OK;
}

extern "C" const char* get_plugin_name() {
    return "GeminiPlugin";
}

extern "C" uint32 ai_plugin_get_capabilities() {
    return AI_CAP_STREAMING;
}
