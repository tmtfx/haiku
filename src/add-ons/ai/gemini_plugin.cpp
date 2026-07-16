// gemini_plugin.cpp
// Plugin per il servizio Google Gemini AI su Haiku - Versione Stateless Concorrente.
#include <os/ai/AIPlugin.h>
#include <os/ai/AINetworkPlugin.h>
#include <os/ai/AICommands.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

//verificare cosa rimuovere qua
#include <Url.h>
#include <UrlRequest.h>
#include <UrlSynchronousRequest.h>
#include <UrlProtocolRoster.h>
#include <UrlRequest.h>
#include <HttpHeaders.h>
#include <HttpRequest.h>
#include <HttpResult.h>
// *****************************

#include <DataIO.h>
#include <String.h>
#include <File.h>
#include <OS.h>
#include <Messenger.h>
#include <Message.h>
#include <Path.h>

#include <Json.h>

typedef void* ai_plugin_t;

using namespace BPrivate::Network;
/*
class SyncListener : public BUrlProtocolListener {
public:
    SyncListener() {}
    virtual ~SyncListener() {}
    bool CertificateVerificationFailed(BUrlRequest* request, BCertificate& certificate, const char* message) override {
        return false; 
    }
};*/

class StreamTarget : public BDataIO {
public:
    StreamTarget(const char* notifyPath) {
        fFile.SetTo(notifyPath, B_WRITE_ONLY | B_CREATE_FILE | B_OPEN_AT_END);
        fBuffer.SetTo("");
    }

    virtual ssize_t Write(const void* buffer, size_t size) override {
        if (fFile.InitCheck() != B_OK || size == 0) return size;

        fBuffer.Append((const char*)buffer, size);

        int32 currentPos = 0;
        while (true) {
            int32 matchPos = fBuffer.FindFirst("\"text\":", currentPos);
            if (matchPos == B_ERROR) break;
            
            matchPos += 7; // after `"text":`
            // Skip spaces or tabs
            while (matchPos < fBuffer.Length() && (fBuffer.ByteAt(matchPos) == ' ' || fBuffer.ByteAt(matchPos) == '\t')) {
                matchPos++;
            }
            if (matchPos >= fBuffer.Length()) break; // Wait for more data
            if (fBuffer.ByteAt(matchPos) != '"') {
                // Skip invalid formatting
                currentPos = matchPos;
                continue;
            }
            matchPos++; // skip opening double-quote
            
            int32 endContent = fBuffer.FindFirst("\"", matchPos);
            while (endContent != B_ERROR && fBuffer.ByteAt(endContent - 1) == '\\') {
                endContent = fBuffer.FindFirst("\"", endContent + 1);
            }

            if (endContent == B_ERROR) break; // Wait for more data

            BString token;
            fBuffer.CopyInto(token, matchPos, endContent - matchPos);
            
            token.ReplaceAll("\\n", "\n");
            token.ReplaceAll("\\t", "\t");
            token.ReplaceAll("\\\"", "\"");
            token.ReplaceAll("\\\\", "\\");

            if (!token.IsEmpty()) {
                fFile.Write(token.String(), token.Length());
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
};
/* in AINetworkPlugin.h
class CompletionListener : public SyncListener {
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
struct GeminiHandle {
    char* base_url;
};
*/
/* usiamo la generica AsyncArgs in AIPlugin.h
struct GeminiAsyncArgs {
    char* api_key;
    char* model;
    char* notify_path;
    char* base_url;
    BMessage* context_copy;
    BMessenger server_messenger;
};
*/

// Estrae in modo sicuro la sottostringa JSON dell'oggetto "args" bilanciando le parentesi graffe
//versione buggata!!!
/*
BString ExtractArgsJson(const BString& json, int32 functionCallPos) {
    int32 argsPos = json.FindFirst("\"args\"", functionCallPos);
    if (argsPos == B_ERROR) return "{}";
    int32 braceStart = json.FindFirst("{", argsPos);
    if (braceStart == B_ERROR) return "{}";
    
    int32 depth = 0;
    for (int32 i = braceStart; i < json.Length(); i++) {
        char c = json.ByteAt(i);
        if (c == '{') depth++;
        else if (c == '}') {
            depth--;
            if (depth == 0) {
                BString result;
                json.CopyInto(result, braceStart, i - braceStart + 1);
                return result;
            }
        }
    }
    return "{}";
}*/
BString ExtractArgsJson(const BString& json, int32 functionCallPos) {
    int32 argsPos = json.FindFirst("\"args\"", functionCallPos);
    if (argsPos == B_ERROR) return "{}";
    int32 braceStart = json.FindFirst("{", argsPos);
    if (braceStart == B_ERROR) return "{}";
    
    int32 depth = 0;
    bool inString = false;
    
    for (int32 i = braceStart; i < json.Length(); i++) {
        char c = json.ByteAt(i);
        
        if (c == '"') {
            // Controlliamo se la virgoletta è preceduta da un numero DISPARI di backslash
            int32 backslashCount = 0;
            for (int32 j = i - 1; j >= braceStart; j--) {
                if (json.ByteAt(j) == '\\') {
                    backslashCount++;
                } else {
                    break;
                }
            }
            
            // Se i backslash precedenti sono pari (o 0), la virgoletta definisce l'inizio/fine di una stringa JSON
            if (backslashCount % 2 == 0) {
                inString = !inString;
            }
        }
        
        // Calcoliamo la profondità delle graffe SOLO se non siamo dentro una stringa di testo
        if (!inString) {
            if (c == '{') {
                depth++;
            } else if (c == '}') {
                depth--;
                if (depth == 0) {
                    BString result;
                    json.CopyInto(result, braceStart, i - braceStart + 1);
                    
                    // Rimuoviamo i ritorni a capo reali e le tabulazioni strutturali 
                    // che potrebbero spaccare il payload JSON durante la formattazione stringa successiva
                    result.ReplaceAll("\n", " ");
                    result.ReplaceAll("\r", " ");
                    result.ReplaceAll("\t", " ");
                    
                    return result;
                }
            }
        }
    }
    return "{}";
}

void AppendToolCallToContext(BMessage* context, const char* name, const char* argsJson, const char* thoughtSignature = nullptr) {
    BMessage messagesMsg;
    if (context->FindMessage("messages", &messagesMsg) != B_OK) {
        context->AddMessage("messages", &messagesMsg);
    }
    int32 i = 0; BMessage dummy;
    while (messagesMsg.FindMessage("msg", i, &dummy) == B_OK) i++;
    
    BMessage toolCallMsg;
    toolCallMsg.AddString("type", "functionCall");
    toolCallMsg.AddString("name", name);
    toolCallMsg.AddString("args", argsJson);
    
    // === NOVITÀ: Se c'è la firma del pensiero, la salviamo nel messaggio ===
    if (thoughtSignature && strlen(thoughtSignature) > 0) {
        toolCallMsg.AddString("thought_signature", thoughtSignature);
    }
    
    messagesMsg.AddMessage("msg", &toolCallMsg);
    context->RemoveName("messages");
    context->AddMessage("messages", &messagesMsg);
}
void AppendToolResponseToContext(BMessage* context, const char* name, const char* responseJson) {
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
    
    messagesMsg.AddMessage("msg", &toolRespMsg);
    context->RemoveName("messages");
    context->AddMessage("messages", &messagesMsg);
}

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

// Funzione helper per iniettare l'intero storico dei messaggi salvato nel BMessage nel JSON di Gemini
void BuildPayloadFromContext(const BMessage* config, const char* currentPrompt, BString& outPayload)
{
    outPayload.SetTo("{\"contents\":[");
    bool first = true;

    BMessage messagesMsg;
    if (config && config->FindMessage("messages", &messagesMsg) == B_OK) {
        BMessage msgItem;
        int32 i = 0;
        
        while (messagesMsg.FindMessage("msg", i, &msgItem) == B_OK || 
               messagesMsg.FindMessage(BString().SetToFormat("%d", i).String(), &msgItem) == B_OK) {
            
            const char* type = nullptr;
            msgItem.FindString("type", &type);

            if (type && strcmp(type, "functionCall") == 0) {
                // Storico di una chiamata a un tool fatta dall'LLM
                const char* name = msgItem.FindString("name");
                const char* args = msgItem.FindString("args");
                const char* thoughtSig = nullptr;
                msgItem.FindString("thought_signature", &thoughtSig);
                
                if (!first) outPayload.Append(",");
                
                BString cleanArgs(args && args[0] != '\0' ? args : "{}");
                // Assicuriamoci che i ritorni a capo reali non escapati dentro args (se presenti) 
                // non spacchino la riga prima di arrivare a thought_signature
                cleanArgs.ReplaceAll("\n", "\\n");
                cleanArgs.ReplaceAll("\r", "\\r");
                
                BString tempCall;
                if (thoughtSig && thoughtSig[0] != '\0') {
                    // === FIX FONDAMENTALE: Applichiamo l'escape sul thought_signature ===
                    BString escapedThought = EscapeStringForJson(thoughtSig);

                    /*tempCall.SetToFormat(
                        "{\"role\":\"model\",\"parts\":["
                        "{\"functionCall\":{\"name\":\"%s\",\"args\":%s},\"thought_signature\":\"%s\"}"
                        "]}", 
                        name, 
                        (args && args[0] != '\0') ? args : "{}", 
                        escapedThought.String()
                    );*/
                    tempCall << "{\"role\":\"model\",\"parts\":[";
                    tempCall << "{\"functionCall\":{\"name\":\"" << name << "\",\"args\":" << cleanArgs << "},";
                    tempCall << "\"thought_signature\":\"" << escapedThought << "\"}";
                    tempCall << "]}";
                } else {
                    tempCall.SetToFormat(
                        "{\"role\":\"model\",\"parts\":["
                        "{\"functionCall\":{\"name\":\"%s\",\"args\":%s}}"
                        "]}", 
                        name, 
                        (args && args[0] != '\0') ? args : "{}"
                    );
                }
                outPayload << tempCall;
                first = false;
            } 
            else if (type && strcmp(type, "functionResponse") == 0) {
                // Storico della risposta del sistema passata all'LLM
                const char* name = msgItem.FindString("name");
                const char* response = msgItem.FindString("response");
                if (!first) outPayload.Append(",");
                
                BString formattedResponse;
                BString respStr(response ? response : "");
                respStr.Trim();
                if (respStr.StartsWith("{") && respStr.EndsWith("}")) {
                    formattedResponse = respStr;
                } else {
                    // Usiamo la funzione centralizzata anche qui
                    BString escapedResp = EscapeStringForJson(respStr.String());
                    formattedResponse.SetToFormat("{\"output\":\"%s\"}", escapedResp.String());
                }
                
                BString tempCall;
                tempCall.SetToFormat("{\"role\":\"function\",\"parts\":[{\"functionResponse\":{\"name\":\"%s\",\"response\":%s}}]}", name, formattedResponse.String());
                outPayload << tempCall;
                first = false;
            } 
            else {
                // Messaggio standard di testo (User o Assistant)
                const char* role = nullptr;
                const char* content = nullptr;
                msgItem.FindString("role", &role);
                
                if (msgItem.FindString("content", &content) != B_OK) {
                    msgItem.FindString("text", &content);
                }

                if (role && content && content[0] != '\0') {
                    if (!first) outPayload.Append(",");
                    
                    BString geminiRole = (strcmp(role, "assistant") == 0) ? "model" : "user";

                    // Usiamo la funzione centralizzata
                    BString escapedContent = EscapeStringForJson(content);

                    BString objectStr;
                    objectStr.SetToFormat("{\"role\":\"%s\",\"parts\":[{\"text\":\"%s\"}]}", 
                                          geminiRole.String(), escapedContent.String());
                    
                    outPayload.Append(objectStr);
                    first = false;
                }
            }
            i++;
        }
    }

    // Il prompt corrente viene aggiunto SEMPRE alla fine, se esiste
    if (currentPrompt && currentPrompt[0] != '\0') {
        if (!first) outPayload.Append(",");
        
        // Usiamo la funzione centralizzata
        BString escapedPrompt = EscapeStringForJson(currentPrompt);
        
        BString objectStr;
        objectStr.SetToFormat("{\"role\":\"user\",\"parts\":[{\"text\":\"%s\"}]}", escapedPrompt.String());
        outPayload.Append(objectStr);
    }

    outPayload.Append("]}");
}
/*
void BuildPayloadFromContext(const BMessage* config, const char* currentPrompt, BString& outPayload)
{
    outPayload.SetTo("{\"contents\":[");
    bool first = true;

    BMessage messagesMsg;
    if (config && config->FindMessage("messages", &messagesMsg) == B_OK) {
        BMessage msgItem;
        int32 i = 0;
        
        while (messagesMsg.FindMessage("msg", i, &msgItem) == B_OK || 
               messagesMsg.FindMessage(BString().SetToFormat("%d", i).String(), &msgItem) == B_OK) {
            
            const char* type = nullptr;
            msgItem.FindString("type", &type);

            if (type && strcmp(type, "functionCall") == 0) {
                // Storico di una chiamata a un tool fatta dall'LLM
                const char* name = msgItem.FindString("name");
                const char* args = msgItem.FindString("args");
                
                const char* thoughtSig = nullptr;
                msgItem.FindString("thought_signature", &thoughtSig);
                
                if (!first) outPayload.Append(",");
                
                //outPayload.AppendFormat("{\"role\":\"model\",\"parts\":[{\"functionCall\":{\"name\":\"%s\",\"args\":%s}}]}", name, (args && args[0] != '\0') ? args : "{}");
                BString tempCall;
                if (thoughtSig && thoughtSig[0] != '\0') {
                    // Inseriamo thought_signature nel part, come fratello di functionCall
                    tempCall.SetToFormat(
                        "{\"role\":\"model\",\"parts\":["
                        "{\"functionCall\":{\"name\":\"%s\",\"args\":%s},\"thought_signature\":\"%s\"}"
                        "]}", 
                        name, 
                        (args && args[0] != '\0') ? args : "{}", 
                        thoughtSig
                    );
                } else {
                    // Fallback standard senza firma
                    tempCall.SetToFormat(
                        "{\"role\":\"model\",\"parts\":["
                        "{\"functionCall\":{\"name\":\"%s\",\"args\":%s}}"
                        "]}", 
                        name, 
                        (args && args[0] != '\0') ? args : "{}"
                    );
                }
                outPayload << tempCall;
                first = false;
            } 
            else if (type && strcmp(type, "functionResponse") == 0) {
                // Storico della risposta del sistema passata all'LLM
                const char* name = msgItem.FindString("name");
                const char* response = msgItem.FindString("response");
                if (!first) outPayload.Append(",");
                
                BString formattedResponse;
                BString respStr(response ? response : "");
                respStr.Trim();
                if (respStr.StartsWith("{") && respStr.EndsWith("}")) {
                    formattedResponse = respStr;
                } else {
                    // Non è un oggetto JSON valido, lo wrappiamo noi facendo l'escape
                    BString escapedResp = respStr;
                    escapedResp.ReplaceAll("\\", "\\\\");
                    escapedResp.ReplaceAll("\"", "\\\"");
                    escapedResp.ReplaceAll("\n", "\\n");
                    escapedResp.ReplaceAll("\r", "\\r");
                    escapedResp.ReplaceAll("\t", "\\t");
                    formattedResponse.SetToFormat("{\"output\":\"%s\"}", escapedResp.String());
                }
                
                BString tempCall;
                tempCall.SetToFormat("{\"role\":\"function\",\"parts\":[{\"functionResponse\":{\"name\":\"%s\",\"response\":%s}}]}", name, formattedResponse.String());
                outPayload << tempCall;
                first = false;
            } 
            else {
                // Messaggio standard di testo (User o Assistant)
                const char* role = nullptr;
                const char* content = nullptr;
                msgItem.FindString("role", &role);
                
                if (msgItem.FindString("content", &content) != B_OK) {
                    msgItem.FindString("text", &content);
                }

                if (role && content && content[0] != '\0') {
                    if (!first) outPayload.Append(",");
                    
                    BString geminiRole = (strcmp(role, "assistant") == 0) ? "model" : "user";

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
            }
            i++;
        }
    }

    // BUG FIX: Il prompt corrente viene aggiunto SEMPRE alla fine, se esiste
    if (currentPrompt && currentPrompt[0] != '\0') {
        if (!first) outPayload.Append(",");
        
        BString escapedPrompt(currentPrompt);
        escapedPrompt.ReplaceAll("\\", "\\\\");
        escapedPrompt.ReplaceAll("\"", "\\\"");
        escapedPrompt.ReplaceAll("\n", "\\n");
        escapedPrompt.ReplaceAll("\r", "\\r");
        escapedPrompt.ReplaceAll("\t", "\\t");
        
        BString objectStr;
        objectStr.SetToFormat("{\"role\":\"user\",\"parts\":[{\"text\":\"%s\"}]}", escapedPrompt.String());
        outPayload.Append(objectStr);
    }

    outPayload.Append("]}");
}*/

extern "C" ai_plugin_t ai_plugin_init(const BMessage* config)
{
    // Usiamo 'new' (con std::nothrow per evitare eccezioni in caso di RAM esaurita)
    // per creare l'oggetto e attivare il costruttore che azzera base_url
    AIPluginHandle* h = new(std::nothrow) AIPluginHandle();
    if (!h) return nullptr;
    
    const char* url = nullptr;
    if (config && config->FindString("base_url", &url) == B_OK) {
        h->base_url = url ? strdup(url) : nullptr;
    }
    
    return (ai_plugin_t)h;
}

extern "C" void ai_plugin_free(ai_plugin_t handle)
{
    AIPluginHandle* h = (AIPluginHandle*)handle;
    delete h;
}
extern "C" status_t ai_plugin_update_config(ai_plugin_t handle, const BMessage* config)
{
    AIPluginHandle* h = (AIPluginHandle*)handle;
    if (!h || !config) return B_BAD_VALUE;
    
    const char* url = nullptr;
    if (config->FindString("base_url", &url) == B_OK) {
        if (h->base_url) free(h->base_url);
        h->base_url = url ? strdup(url) : nullptr;
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
    
    // Cast alla nuova struct condivisa ed elegante!
    AIPluginHandle* h = (AIPluginHandle*)handle;
    
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

    if (!model || model[0] == '\0') model = "gemini-3.5-flash";
    
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
        fprintf(stderr, "[GEMINI PLUGIN] ERRORE: BUrlProtocolRoster::MakeRequest ha fallito!\n");
        delete out; 
        // Sì, serve! Scrivere l'errore nel buffer evita che il client usi dati casuali rimasti in memoria.
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
    
    if (statusCode != 200) {
        fprintf(stderr, "[GEMINI PLUGIN] Dettaglio errore server remoto:\n%.*s\n", (int)len, (const char*)buf);
        snprintf(response_buf, response_len, "{\"error\":\"http error %d\"}", (int)statusCode); 
        delete req; delete out; 
        return B_ERROR; 
    }
    
    bool extractedSuccessfully = false;
    BString rawResponse((const char*)buf, len);

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
            
            if (!config->HasString("title") && prompt != nullptr) {
                BString autoTitle(prompt);
                autoTitle.Trim();
                if (autoTitle.Length() > 30) {
                    autoTitle.Truncate(30);
                    autoTitle << "...";
                }
                config->RemoveName("title");
                config->AddString("title", autoTitle.String());
            
                // TODO: Qui notifichi l'applicazione principale (se necessario) che il BMessage config è aggiornato
            }
        }
    }

    if (!extractedSuccessfully) {
        fprintf(stderr, "[GEMINI PLUGIN] ATTENZIONE: Parsing strutturato fallito, restituzione testo raw.\n");
        size_t copy_len = len < response_len - 1 ? len : response_len - 1;
        memcpy(response_buf, buf, copy_len);
        response_buf[copy_len] = '\0';
    }

    delete req; 
    delete out;
    fprintf(stderr, "[GEMINI PLUGIN] === FINE generate_text_sync ===\n\n");
    return rc == B_OK ? 0 : -1;
}
/*
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
*/
/*
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
}*/
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
/*
void ConvertBMessageToGeminiToolsJson(const BMessage* toolsMsg, BString& outJson)
{
    outJson = "[";
    
    BMessage tool;
    int32 i = 0;
    bool first = true;
    
    // Cicliamo direttamente sui messaggi "tool" presenti nella radice di toolsMsg
    while (toolsMsg->FindMessage("tool", i, &tool) == B_OK) {
        if (!first) outJson << ",";
        first = false;
        
        const char* name = tool.FindString("name");
        const char* desc = tool.FindString("description");
        
        BMessage params;
        tool.FindMessage("parameters", &params);
        
        outJson << "{";
        outJson << "\"name\":\"" << name << "\",";
        outJson << "\"description\":\"" << desc << "\"";
        
        if (!params.IsEmpty()) {
            BString jsonParams;
            SerializeBMessageToJson(&params, jsonParams);
            outJson << ",\"parameters\":" << jsonParams;
        }
        
        outJson << "}";
        i++;
    }
    
    outJson << "]";
}*/
void ConvertBMessageToGeminiToolsJson(const BMessage* toolsMsg, BString& outJson)
{
    outJson = "[{\"functionDeclarations\":["; // Involucro Gemini

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
        escapedDesc.ReplaceAll("\"","\\\"");
        escapedDesc.ReplaceAll("\n", "\\n");
        escapedDesc.ReplaceAll("\r", "\\r");
        escapedDesc.ReplaceAll("\t", "\\t");

        outJson << "{";
        outJson << "\"name\":\"" << name << "\",";
        outJson << "\"description\":\"" << escapedDesc << "\"";

        BMessage params;
        tool.FindMessage("parameters", &params);
        if (!params.IsEmpty()) {
            BString jsonParams;
            SerializeBMessageToJson(&params, jsonParams);
            outJson << ",\"parameters\":" << jsonParams;
        } else {
            outJson << ",\"parameters\":{\"type\":\"OBJECT\",\"properties\":{}}";
        }

        outJson << "}";
        i++;
    }

    outJson << "]}]"; // Chiusura involucro Gemini
    if (first) outJson.SetTo("");
}

static status_t gemini_stream_thread_func(void* data)
{
    fprintf(stderr, "[GEMINI STREAM WORKER] Thread avviato.\n");
    
    // Cast alla nuova struct globale e condivisa
    AsyncArgs* args = (AsyncArgs*)data;
    if (!args) return B_BAD_VALUE;

    BMessage replyTools;
    bool mcpActive = false;
    bool executionLoop = true;

    // Controllo di sicurezza sulla chiave API
    if (!args->api_key || args->api_key[0] == '\0') {
        fprintf(stderr, "[GEMINI STREAM WORKER] ERRORE CRITICO: API key assente!\n");
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
            // Controlliamo se la risposta contiene almeno un tool prima di attivare l'MCP
            if (replyTools.HasMessage("tool", 0)) {
                mcpActive = true;
            }
        }
    }

    // === CASO 1: MODALITÀ STANDARD (STREAMING NATIVO RETROCOMPATIBILE) ===
    if (!mcpActive) {
        fprintf(stderr, "[GEMINI STREAM WORKER] Modalità Standard: Avvio Streaming Diretto.\n");
        
        BString url;
        if (args->base_url && args->base_url[0] != '\0') {
            url << args->base_url;
        } else {
            url.SetToFormat("https://generativelanguage.googleapis.com/v1beta/models/%s:streamGenerateContent", args->model);
        }
        url << "?key=" << args->api_key;

        BString payload;
        BuildPayloadFromContext(args->context_copy, nullptr, payload);

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
                delete req;
            }
        }
        goto thread_cleanup;
    }

    // === CASO 2: MODALITÀ MCP ATTIVA (LOOP STRUMENTI VIA IPC MESSENGER) ===
    fprintf(stderr, "[GEMINI STREAM WORKER] Modalità MCP Attiva: Avvio loop di interazione strumenti.\n");
    
    executionLoop = true;
    while (executionLoop) {
    	// =====================================================================
        // NUOVO: CONTROLLO INTERRUZIONE PREVENTIVO VIA IPC
        // =====================================================================
        if (args->server_messenger.IsValid()) {
            BMessage checkAbortMsg('CHAB'); // Un 'what' dedicato, es: MSG_CHECK_ABORT
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
                    fprintf(stderr, "[GEMINI STREAM WORKER] Rilevata interruzione asincrona dall'utente. Esco.\n");
                    executionLoop = false;
                    break;
                }
            }
        }
        // =====================================================================
    	
        BString url;
        if (args->base_url && args->base_url[0] != '\0') {
            url << args->base_url;
        } else {
            url.SetToFormat("https://generativelanguage.googleapis.com/v1beta/models/%s:generateContent", args->model);
        }
        url << "?key=" << args->api_key;

        BString payload;
        BuildPayloadFromContext(args->context_copy, nullptr, payload);
/*
        if (mcpActive) {
            if (payload.EndsWith("}")) {
                payload.Truncate(payload.Length() - 1);
                
                BString geminiToolsJson;
                ConvertBMessageToGeminiToolsJson(&replyTools, geminiToolsJson);
                
                if (geminiToolsJson.Length() > 0) {
                    payload << ",\"tools\":[{\"functionDeclarations\":" << geminiToolsJson << "}]}";
                } else {
                    payload << "}";
                }
            }
        }*/
        if (mcpActive) {
            BString geminiToolsJson;
            ConvertBMessageToGeminiToolsJson(&replyTools, geminiToolsJson);

            if (geminiToolsJson.Length() > 0) {
                // Invece di fare Truncate(length - 1) sperando che l'ultimo carattere sia '}', 
                // cerchiamo l'ultima parentesi di chiusura reale dell'oggetto root.
                int32 lastCloseBrace = payload.FindLast("}");
                if (lastCloseBrace != B_ERROR) {
                    // Tagliamo la stringa esattamente all'ultima graffa di chiusura globale
                    payload.Truncate(lastCloseBrace);
                    // Iniettiamo i tool a livello radice e richiudiamo in sicurezza
                    payload << ",\"tools\":" << geminiToolsJson << "}";
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
            fprintf(stderr, "[GEMINI STREAM WORKER] Fallito il parsing JSON della risposta di rete.\n");
            executionLoop = false;
            break;
        }

        BMessage candidates, candidateZero, content, parts, partZero, functionCall;
        const char* toolName = nullptr;
        const char* textContent = nullptr;

        if (parsedJson.FindMessage("candidates", &candidates) == B_OK
            && candidates.FindMessage("0", &candidateZero) == B_OK
            && candidateZero.FindMessage("content", &content) == B_OK
            && content.FindMessage("parts", &parts) == B_OK
            && parts.FindMessage("0", &partZero) == B_OK) {

            // Sotto-caso A: Gemini vuole eseguire un tool
            if (partZero.FindMessage("functionCall", &functionCall) == B_OK
                && functionCall.FindString("name", &toolName) == B_OK) {
                
                // Estraiamo la thought_signature inviata da Google
                const char* thoughtSignature = nullptr;
                if (partZero.FindString("thought_signature", &thoughtSignature) == B_OK) {
                    // Trovato in partZero (snake_case)
                } else if (partZero.FindString("thoughtSignature", &thoughtSignature) == B_OK) {
                    // Trovato in partZero (camelCase)
                } else if (functionCall.FindString("thought_signature", &thoughtSignature) == B_OK) {
                    // Trovato in functionCall (snake_case)
                } else {
                    // Trovato in functionCall (camelCase) o non presente
                    functionCall.FindString("thoughtSignature", &thoughtSignature);
                }

                int32 fCallPos = rawResponse.FindFirst("\"functionCall\"");
                BString argsJson = ExtractArgsJson(rawResponse, fCallPos);

                fprintf(stderr, "[GEMINI MCP] L'LLM richiede lo strumento: %s con argomenti: %s\n", toolName, argsJson.String());

                // Invochiamo lo strumento mandando un messaggio sincrono all'ai_server
                BMessage reqExec(MSG_EXECUTE_TOOL);
                reqExec.AddString("name", toolName);
                reqExec.AddString("args", argsJson.String());
                
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
                        // Se la risposta è già un JSON valido, la teniamo così
                        if (testStr.StartsWith("{") || testStr.StartsWith("[")) {
                            toolResultBuf = resStr;
                        } else {
                            // Altrimenti la incapsuliamo in un JSON pulito
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
                AppendToolCallToContext(args->context_copy, toolName, argsJson.String(), thoughtSignature);
                AppendToolResponseToContext(args->context_copy, toolName, toolResultBuf.String());
                
            } 
            // Sotto-caso B: Gemini ha terminato la catena e restituisce il testo finale
            else if (partZero.FindString("text", &textContent) == B_OK) {
                fprintf(stderr, "[GEMINI MCP] Risposta testuale finale ricevuta.\n");
                BFile streamFile(args->notify_path, B_WRITE_ONLY | B_CREATE_FILE | B_OPEN_AT_END);
                if (streamFile.InitCheck() == B_OK) {
                    streamFile.Write(textContent, strlen(textContent));
                } else {
                    fprintf(stderr, "[GEMINI MCP] ERRORE: Impossibile creare/aprire il file di notifica!\n");
                }
                executionLoop = false; // Abbiamo il testo finale, usciamo dal loop!
            }
        } else {
            // === RECUPERO E LOGGING DELL'ERRORE ===
            fprintf(stderr, "[GEMINI STREAM WORKER] ERRORE: Risposta di rete non valida o priva di 'candidates'.\n");
            fprintf(stderr, "[GEMINI STREAM WORKER] Risposta grezza ricevuta:\n%s\n", rawResponse.String());
            
            BMessage errorObj;
            if (parsedJson.FindMessage("error", &errorObj) == B_OK) {
                const char* errMsg = nullptr;
                errorObj.FindString("message", &errMsg);
                if (errMsg) {
                    fprintf(stderr, "[GEMINI STREAM WORKER] Dettaglio errore API di Google: %s\n", errMsg);
                    
                    BFile streamFile(args->notify_path, B_WRITE_ONLY | B_OPEN_AT_END);
                    if (streamFile.InitCheck() == B_OK) {
                        BString guiError;
                        guiError.SetToFormat("\n[Errore API Gemini: %s]\n", errMsg);
                        streamFile.Write(guiError.String(), guiError.Length());
                    }
                }
            }
            executionLoop = false;
        }
    }
    // =========================================================================
    // === GENERAZIONE TITOLO POST-RISPOSTA IN MODALITÀ ASINCRONA (MCP) ===
    // =========================================================================
    if (args->context_copy != nullptr && !args->context_copy->HasString("title")) {
        BMessage messagesMsg;
        const char* firstPromptText = nullptr;

        // Recuperiamo l'ultimo prompt reale inviato dall'utente nello storico dei messaggi
        if (args->context_copy->FindMessage("messages", &messagesMsg) == B_OK) {
            int32 msgCount = 0;
            BMessage msgItem;
            
            while (messagesMsg.FindMessage("msg", msgCount, &msgItem) == B_OK || 
                   messagesMsg.FindMessage(BString().SetToFormat("%d", msgCount).String(), &msgItem) == B_OK) {
                
                const char* role = msgItem.FindString("role");
                const char* content = msgItem.FindString("content");
                if (!content) msgItem.FindString("text", &content);
                
                if (role && strcmp(role, "user") == 0 && content && content[0] != '\0') {
                    firstPromptText = content; // Sovrascrive fino a beccare l'ultimo
                }
                msgCount++;
            }
        }

        // Se abbiamo trovato il testo del prompt originario utente, generiamo l'auto-titolo
        if (firstPromptText && firstPromptText[0] != '\0') {
            BString autoTitle(firstPromptText);
            autoTitle.Trim();
            if (autoTitle.Length() > 30) {
                autoTitle.Truncate(30);
                autoTitle << "...";
            }
            
            // 1. Aggiorniamo la copia locale del thread
            args->context_copy->RemoveName("title");
            args->context_copy->AddString("title", autoTitle.String());
            
            // 2. Notifichiamo la finestra/applicazione principale via IPC BMessenger 
            if (args->server_messenger.IsValid()) {
                BMessage titleUpdateMsg('UTIT'); // Assicurati che 'UTIT' sia l'ID che si aspetta la tua app
                titleUpdateMsg.AddString("title", autoTitle.String());
                args->server_messenger.SendMessage(&titleUpdateMsg);
            }
            fprintf(stderr, "[GEMINI ASYNC] Auto-titolo generato post-risposta: '%s'\n", autoTitle.String());
        }
    }

    // Scriviamo il terminatore ufficiale sul file tmp per svegliare il Watcher del server
    {
        BFile streamFile(args->notify_path, B_WRITE_ONLY | B_CREATE_FILE | B_OPEN_AT_END);
        if (streamFile.InitCheck() == B_OK) {
            BString endMarker = "<<STREAM_END>>";
            streamFile.Write(endMarker.String(), endMarker.Length());
        }
    }

thread_cleanup:
    fprintf(stderr, "[GEMINI STREAM WORKER] Pulizia e chiusura thread worker.\n");
    
    // Questa singola riga C++ sostituisce tutte le free() manuali, 
    // perché lo sanno fare da sole il distruttore di AsyncArgs!
    delete args; 

    return B_OK;
}
/* funzionante prima della centralizzazione
static status_t gemini_stream_thread_func(void* data)
{
    fprintf(stderr, "[GEMINI STREAM WORKER] Thread avviato.\n");
    GeminiAsyncArgs* args = (GeminiAsyncArgs*)data;
    
    if (!args) return B_BAD_VALUE;

    BMessage replyTools;
    bool mcpActive = false;
    bool executionLoop = true;

    // Controllo di sicurezza sulla chiave API
    if (!args->api_key || args->api_key[0] == '\0') {
        fprintf(stderr, "[GEMINI STREAM WORKER] ERRORE CRITICO: API key assente!\n");
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
            // Controlliamo se la risposta contiene almeno un tool prima di attivare l'MCP
            if (replyTools.HasMessage("tool", 0)) {
                mcpActive = true;
            }
        }
    }

    // === CASO 1: MODALITÀ STANDARD (STREAMING NATIVO RETROCOMPATIBILE) ===
    if (!mcpActive) {
        fprintf(stderr, "[GEMINI STREAM WORKER] Modalità Standard: Avvio Streaming Diretto.\n");
        
        BString url;
        if (args->base_url && args->base_url[0] != '\0') {
            url << args->base_url;
        } else {
            url.SetToFormat("https://generativelanguage.googleapis.com/v1beta/models/%s:streamGenerateContent", args->model);
        }
        url << "?key=" << args->api_key;

        BString payload;
        BuildPayloadFromContext(args->context_copy, nullptr, payload);

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
                delete req;
            }
        }
        goto thread_cleanup;
    }

    // === CASO 2: MODALITÀ MCP ATTIVA (LOOP STRUMENTI VIA IPC MESSENGER) ===
    fprintf(stderr, "[GEMINI STREAM WORKER] Modalità MCP Attiva: Avvio loop di interazione strumenti.\n");
    
    executionLoop = true;
    while (executionLoop) {
        BString url;
        if (args->base_url && args->base_url[0] != '\0') {
            url << args->base_url;
        } else {
            url.SetToFormat("https://generativelanguage.googleapis.com/v1beta/models/%s:generateContent", args->model);
        }
        url << "?key=" << args->api_key;

        BString payload;
        BuildPayloadFromContext(args->context_copy, nullptr, payload);

        // Iniettiamo i tool disponibili recuperati via IPC dall'ai_server
        //if (availableTools.Length() > 0) {
        //    if (payload.EndsWith("}")) {
        //        payload.Truncate(payload.Length() - 1);
        //        //payload.AppendFormat(",\"tools\":[{\"functionDeclarations\":%s}]}", availableTools.String());
        //        BString toolsFormatted;
        //        toolsFormatted.SetToFormat(",\"tools\":[{\"functionDeclarations\":%s}]}", availableTools.String());
        //        payload << toolsFormatted;
        //    }
        //}
        if (mcpActive) {
            if (payload.EndsWith("}")) {
                payload.Truncate(payload.Length() - 1);
                
                BString geminiToolsJson;
                ConvertBMessageToGeminiToolsJson(&replyTools, geminiToolsJson);
                
                if (geminiToolsJson.Length() > 0) {
                    payload << ",\"tools\":[{\"functionDeclarations\":" << geminiToolsJson << "}]}";
                } else {
                    payload << "}";
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
            fprintf(stderr, "[GEMINI STREAM WORKER] Fallito il parsing JSON della risposta di rete.\n");
            executionLoop = false;
            break;
        }

        BMessage candidates, candidateZero, content, parts, partZero, functionCall;
        const char* toolName = nullptr;
        const char* textContent = nullptr;

        if (parsedJson.FindMessage("candidates", &candidates) == B_OK
            && candidates.FindMessage("0", &candidateZero) == B_OK
            && candidateZero.FindMessage("content", &content) == B_OK
            && content.FindMessage("parts", &parts) == B_OK
            && parts.FindMessage("0", &partZero) == B_OK) {

            // Sotto-caso A: Gemini vuole eseguire un tool
            if (partZero.FindMessage("functionCall", &functionCall) == B_OK
                && functionCall.FindString("name", &toolName) == B_OK) {
                
                // === NOVITÀ: Estraiamo la thought_signature inviata da Google ===
                const char* thoughtSignature = nullptr;
                if (partZero.FindString("thought_signature", &thoughtSignature) == B_OK) {
                    // Caso 1: Trovato in partZero (snake_case)
                } else if (partZero.FindString("thoughtSignature", &thoughtSignature) == B_OK) {
                    // Caso 2: Trovato in partZero (camelCase)
                } else if (functionCall.FindString("thought_signature", &thoughtSignature) == B_OK) {
                    // Caso 3: Trovato in functionCall (snake_case)
                } else {
                    // Caso 4: Trovato in functionCall (camelCase) o non presente
                    functionCall.FindString("thoughtSignature", &thoughtSignature);
                }

                int32 fCallPos = rawResponse.FindFirst("\"functionCall\"");
                BString argsJson = ExtractArgsJson(rawResponse, fCallPos);

                fprintf(stderr, "[GEMINI MCP] L'LLM richiede lo strumento: %s con argomenti: %s\n", toolName, argsJson.String());

                // Invochiamo lo strumento mandando un messaggio sincrono all'ai_server
                BMessage reqExec(MSG_EXECUTE_TOOL);
                reqExec.AddString("name", toolName);
                reqExec.AddString("args", argsJson.String());
                
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
                        // Se la risposta è già un JSON valido (inizia con { o [), la teniamo così
                        if (testStr.StartsWith("{") || testStr.StartsWith("[")) {
                            toolResultBuf = resStr;
                        } else {
                            // Se è testo semplice (o vuoto), lo incapsuliamo in un JSON pulito per Gemini
                            BString escapedRes = EscapeStringForJson(resStr);
                            //toolResultBuf.SetToFormat("{\"output\":\"%s\"}", resStr);
                            toolResultBuf.SetToFormat("{\"output\":\"%s\"}", escapedRes.String());
                        }
                    } else {
                        // Se il server ha risposto con una stringa vuota (come nel tuo caso!)
                        toolResultBuf = "{\"error\":\"Il comando sul server ha restituito una risposta vuota.\"}";
                    }
                } else {
                    toolResultBuf = "{\"error\":\"Esecuzione dello strumento fallita via IPC BMessenger\"}";
                }

                // Aggiorniamo la history clonata in memoria per il prossimo turno del loop
                //AppendToolCallToContext(args->context_copy, toolName, argsJson.String());
                AppendToolCallToContext(args->context_copy, toolName, argsJson.String(), thoughtSignature);
                AppendToolResponseToContext(args->context_copy, toolName, toolResultBuf.String());
                
                // Il loop continua (executionLoop = true), inviando il risultato a Gemini al prossimo ciclo
            } 
            // Sotto-caso B: Gemini ha terminato la catena e restituisce il testo finale
            else if (partZero.FindString("text", &textContent) == B_OK) {
                fprintf(stderr, "[GEMINI MCP] Risposta testuale finale ricevuta.\n");
                BFile streamFile(args->notify_path, B_WRITE_ONLY | B_CREATE_FILE | B_OPEN_AT_END);
                if (streamFile.InitCheck() == B_OK) {
                    streamFile.Write(textContent, strlen(textContent));
                } else {
					fprintf(stderr, "[GEMINI MCP] ERRORE: Impossibile creare/aprire il file di notifica!\n");
    			}
                executionLoop = false; // Abbiamo il testo finale, usciamo dal loop!
            }
        } else {
        	// === RECUPERO E LOGGING DELL'ERRORE ===
            fprintf(stderr, "[GEMINI STREAM WORKER] ERRORE: Risposta di rete non valida o priva di 'candidates'.\n");
            fprintf(stderr, "[GEMINI STREAM WORKER] Risposta grezza ricevuta:\n%s\n", rawResponse.String());
            
            // Tentiamo di estrarre l'errore strutturato inviato da Google per mostrarlo in UI
            BMessage errorObj;
            if (parsedJson.FindMessage("error", &errorObj) == B_OK) {
                const char* errMsg = nullptr;
                errorObj.FindString("message", &errMsg);
                if (errMsg) {
                    fprintf(stderr, "[GEMINI STREAM WORKER] Dettaglio errore API di Google: %s\n", errMsg);
                    
                    // Scriviamo l'errore nel file temporaneo per mostrarlo nell'interfaccia utente
                    BFile streamFile(args->notify_path, B_WRITE_ONLY | B_OPEN_AT_END);
                    if (streamFile.InitCheck() == B_OK) {
                        BString guiError;
                        guiError.SetToFormat("\n[Errore API Gemini: %s]\n", errMsg);
                        streamFile.Write(guiError.String(), guiError.Length());
                    }
                }
            }
            executionLoop = false;
        }
    }

    // Scriviamo il terminatore ufficiale sul file tmp per svegliare il Watcher del server
    {
        BFile streamFile(args->notify_path, B_WRITE_ONLY | B_CREATE_FILE | B_OPEN_AT_END);
        if (streamFile.InitCheck() == B_OK) {
            BString endMarker = "<<STREAM_END>>";
            streamFile.Write(endMarker.String(), endMarker.Length());
        }
    }

thread_cleanup:
    fprintf(stderr, "[GEMINI STREAM WORKER] Pulizia e chiusura thread worker.\n");
    if (args->api_key) free(args->api_key);
    if (args->model) free(args->model);
    if (args->notify_path) free(args->notify_path);
    if (args->base_url) free(args->base_url);
    if (args->context_copy) delete args->context_copy;
    free(args);

    return B_OK;
}*/
/*
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
}*/
/*
extern "C" status_t ai_plugin_generate_text_async(ai_plugin_t handle, 
                                                  const char* prompt, 
                                                  BMessage* config)
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
    args->context_copy = new BMessage(*config);
    
    // === NUOVA LOGICA: Sincronizzazione IPC via BMessenger ===
    // Peschiamo il messenger dell'ai_server inserito nel BMessage di configurazione
    BMessenger serverMessenger;
    if (config->FindMessenger("server_messenger", &serverMessenger) == B_OK) {
        args->server_messenger = serverMessenger;
    } else {
        // Se per qualche motivo manca, inizializziamo un messenger vuoto/non valido
        args->server_messenger = BMessenger();
    }

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
}*/
extern "C" status_t ai_plugin_generate_text_async(ai_plugin_t handle, 
                                                  const char* prompt, 
                                                  BMessage* config)
{
    // Cast alla nuova struct di base dei plugin
    AIPluginHandle* h = (AIPluginHandle*)handle;
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
    
    // Allochiamo con 'new' la struct condivisa AsyncArgs
    AsyncArgs* args = new(std::nothrow) AsyncArgs();
    if (!args) return B_ERROR;
    
    // Inizializzazione pulita senza dupstr_or_null
    args->api_key = apiKey ? strdup(apiKey) : nullptr;
    args->model = strdup(model && model[0] ? model : "gemini-3.5-flash");
    args->notify_path = notifyPath ? strdup(notifyPath) : nullptr;
    args->base_url = (h && h->base_url) ? strdup(h->base_url) : nullptr;
    args->context_copy = new BMessage(*config);
    
    // === Sincronizzazione IPC via BMessenger ===
    BMessenger serverMessenger;
    if (config->FindMessenger("server_messenger", &serverMessenger) == B_OK) {
        args->server_messenger = serverMessenger;
    } else {
        args->server_messenger = BMessenger();
    }

    thread_id thread = spawn_thread(
        gemini_stream_thread_func,
        "gemini_stream_worker",
        B_NORMAL_PRIORITY,
        args
    );

    if (thread < B_OK) {
        fprintf(stderr, "[GEMINI_PLUGIN] Errore: spawn_thread fallito.\n");
        delete args; // Distrugge TUTTO in automatico richiamando il distruttore!
        return B_ERROR;
    }

    resume_thread(thread);
    return B_OK;
}

extern "C" uint32 ai_plugin_get_capabilities() {
    // Comunichiamo ufficialmente all'ai_server che supportiamo sia lo streaming che l'MCP!
    return AI_CAP_STREAMING | AI_CAP_MCP;
}

extern "C" status_t ai_plugin_list_models(const BMessage* config, char* out_buf, size_t out_len)
{
    const char* defaultFallback = "[\"gemini-3.5-flash\"]";
    
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
        const char* fallback = "[\"gemini-3.5-flash\"]";
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
        cleanJson = "[\"gemini-3.5-flash\"]";
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

