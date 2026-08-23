// gemini_plugin.cpp
// Plugin per il servizio Google Gemini AI su Haiku - Versione Stateless Concorrente.
#include <os/ai/AIPlugin.h>
#include <os/ai/AINetworkPlugin.h>
#include <os/ai/AICommands.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <new>

//verificare cosa rimuovere qua
#include <Url.h>
#include <UrlRequest.h>
#include <UrlSynchronousRequest.h>
#include <UrlProtocolRoster.h>
//#include <UrlRequest.h>
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

#define DEFAULT_GEMINI_BASE_URL "https://generativelanguage.googleapis.com"
#define DEFAULT_GEMINI_MODEL "gemini-3.5-flash"

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
                fFile.Flush();
            }
            
            currentPos = endContent + 1;
        }

        if (currentPos > 0) {
            fBuffer.Remove(0, currentPos);
        }

        return size;
    }
    
    const BString& RawResponse() const {
        return fRawResponse;
    }
    
private:
    BFile   fFile;
    BString fBuffer;
    BString fRawResponse;
};

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
}*/


static char* dupstr_or_null(const char* s) {
    if (!s) return nullptr;
    size_t n = strlen(s) + 1;
    char* p = (char*)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

void AppendToolCallToContext(BMessage* context, const char* name, const BMessage* argsMsg, const char* thoughtSignature = nullptr) {
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
void BuildPayloadFromContext(const BMessage* config, const char* currentPrompt, BString& outPayload, const BString* geminiToolsJson = nullptr){
    outPayload.SetTo("{\"contents\":[");
    bool first = true;

    BMessage messagesMsg;
    bool hasHistory = false;
    BMessage lastMsgItem;

    if (config && config->FindMessage("messages", &messagesMsg) == B_OK) {
        hasHistory = true;
        BMessage msgItem;
        int32 i = 0;
        
        while (messagesMsg.FindMessage("msg", i, &msgItem) == B_OK || 
               messagesMsg.FindMessage(BString().SetToFormat("%d", i).String(), &msgItem) == B_OK) {
            
            lastMsgItem = msgItem; // Salviamo l'ultimo messaggio processato

            const char* type = nullptr;
            msgItem.FindString("type", &type);

            if (type && strcmp(type, "functionCall") == 0) {
                // Storico di una chiamata a un tool fatta dall'LLM
                const char* name = msgItem.FindString("name");
                const char* thoughtSig = nullptr;
                msgItem.FindString("thought_signature", &thoughtSig);
                
                if (!first) outPayload.Append(",");
                
                BString cleanArgs;
                BMessage argsMsg;
                if (msgItem.FindMessage("args", &argsMsg) == B_OK) {
                    SerializeBMessageToJson(&argsMsg, cleanArgs);
                } else {
                    const char* argsStr = msgItem.FindString("args");
                    cleanArgs = (argsStr && argsStr[0] != '\0' ? argsStr : "{}");
                }
                
                cleanArgs.ReplaceAll("\n", "\\n");
                cleanArgs.ReplaceAll("\r", "\\r");
                
                BString tempCall;
                if (thoughtSig && thoughtSig[0] != '\0') {
                    BString escapedThought = EscapeStringForJson(thoughtSig);

                    tempCall << "{\"role\":\"model\",\"parts\":[";
                    tempCall << "{\"functionCall\":{\"name\":\"" << name << "\",\"args\":" << cleanArgs << "},";
                    tempCall << "\"thought_signature\":\"" << escapedThought << "\"}";
                    tempCall << "]}";
                } else {
                    tempCall << "{\"role\":\"model\",\"parts\":[";
                    tempCall << "{\"functionCall\":{\"name\":\"" << name << "\",\"args\":" << cleanArgs << "}}";
                    tempCall << "]}";
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
                    
                    BString geminiRole = (strcmp(role, "assistant") == 0 || strcmp(role, "model") == 0) ? "model" : "user";
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

    // Controlliamo se currentPrompt deve essere aggiunto oppure è già presente in fondo allo storico
    if (currentPrompt && currentPrompt[0] != '\0') {
        bool alreadyAppended = false;

        if (hasHistory) {
            const char* lastRole = nullptr;
            const char* lastContent = nullptr;
            lastMsgItem.FindString("role", &lastRole);
            if (lastMsgItem.FindString("content", &lastContent) != B_OK) {
                lastMsgItem.FindString("text", &lastContent);
            }

            if (lastRole && strcmp(lastRole, "user") == 0 &&
                lastContent && strcmp(lastContent, currentPrompt) == 0) {
                alreadyAppended = true;
            }
        }

        if (!alreadyAppended) {
            if (!first) outPayload.Append(",");
            
            BString escapedPrompt = EscapeStringForJson(currentPrompt);
            BString objectStr;
            objectStr.SetToFormat("{\"role\":\"user\",\"parts\":[{\"text\":\"%s\"}]}", escapedPrompt.String());
            outPayload.Append(objectStr);
        }
    }

    outPayload.Append("]");
    
    BString systemPrompt;
    if (config) {
        const char* sys = nullptr;
        if (config->FindString("system_prompt", &sys) == B_OK && sys != nullptr) {
            systemPrompt = sys;
        }
    }
    
    if (systemPrompt.Length() > 0) {
        outPayload << ",\"systemInstruction\":{\"parts\":[{\"text\":\"" << EscapeStringForJson(systemPrompt.String()) << "\"}]}";
    }
    
    if (geminiToolsJson && geminiToolsJson->Length() > 0) {
        outPayload << ",\"tools\":" << *geminiToolsJson;
    }
    
    outPayload.Append("}");
}
/*
// Helper per inviare la segnalazione di errore via Messenger
static void DispatchGeminiError(const BMessenger& messenger, int32 httpCode, 
                                int32 sessionID, const char* ctxId, const BString& rawResponse)
{
    if (!messenger.IsValid()) return;

    BMessage errorReport(MSG_LLM_ERROR);
    errorReport.AddString("plugin_name", "GeminiPlugin");
    errorReport.AddInt32("http_code", httpCode > 0 ? httpCode : 400);
    
    if (sessionID != -1)
        errorReport.AddInt32("session_id", sessionID);
    if (ctxId)
        errorReport.AddString("session_id", ctxId);

    BMessage parsedJson;
    BMessage errorObj;
    if (!rawResponse.IsEmpty() && BJson::Parse(rawResponse.String(), parsedJson) == B_OK 
        && parsedJson.FindMessage("error", &errorObj) == B_OK) {
        
        const char* errMsg = errorObj.FindString("message");
        const char* errStatus = errorObj.FindString("status");
        int32 codeVal = 0;
        errorObj.FindInt32("code", &codeVal);

        if (errMsg) errorReport.AddString("error_message", errMsg);
        if (errStatus) errorReport.AddString("error_type", errStatus);
        if (codeVal != 0) {
            BString cStr;
            cStr << codeVal;
            errorReport.AddString("error_code", cStr.String());
        }
    } else {
        if (!rawResponse.IsEmpty()) {
            errorReport.AddString("error_message", rawResponse.String());
        } else {
            errorReport.AddString("error_message", "Errore di connessione o risposta non valida dal server backend Gemini.");
        }
    }

    messenger.SendMessage(&errorReport);
}*/

extern "C" ai_plugin_t ai_plugin_init(void)
{
    // Usiamo 'new' (con std::nothrow per evitare eccezioni in caso di RAM esaurita)
    // per creare l'oggetto e attivare il costruttore che azzera base_url
    AIPluginHandle* h = new(std::nothrow) AIPluginHandle();
    return (ai_plugin_t)h;
}

extern "C" void ai_plugin_free(ai_plugin_t handle)
{
    AIPluginHandle* h = (AIPluginHandle*)handle;
    delete h;
}
extern "C" status_t 
ai_plugin_generate_text_sync(ai_plugin_t handle,
                             const char* prompt,
                             char* response_buf,
                             size_t response_len,
                             BMessage* config)
{
    fprintf(stderr, "[GEMINI PLUGIN] === INIZIO generate_text_sync ===\n");
    
    if (!config) {
        fprintf(stderr, "[GEMINI PLUGIN] ERRORE: il puntatore BMessage* config è NULL!\n");
        return B_ERROR;
    }
    if (!response_buf || response_len == 0) return B_ERROR;
    
    const char* apiKeyRaw = nullptr;
    config->FindString("api_key", &apiKeyRaw);
    if (!apiKeyRaw || apiKeyRaw[0] == '\0') {
        fprintf(stderr, "[GEMINI PLUGIN] ERRORE CRITICO: api_key non trovata o vuota nel BMessage!\n");
        snprintf(response_buf, response_len, "[gemini_plugin] error: no API key provided");
        return B_ERROR;
    }
    BString apiKey(apiKeyRaw);
    
    const char* baseUrlRaw = nullptr;
    config->FindString("base_url", &baseUrlRaw);
    BString baseUrl = (baseUrlRaw && baseUrlRaw[0] != '\0') ? baseUrlRaw : DEFAULT_GEMINI_BASE_URL;
    if (baseUrl.EndsWith("/")) baseUrl.Truncate(baseUrl.Length() - 1);

    const char* modelName = nullptr;
    config->FindString("model_name", &modelName);
    if (!modelName || modelName[0] == '\0')
        modelName = DEFAULT_GEMINI_MODEL;

    const char* ctxId = nullptr;
    config->FindString("context_id", &ctxId);

    int32 sessionID = -1;
    config->FindInt32("session_id", &sessionID);

    BMessenger serverMessenger;
    config->FindMessenger("server_messenger", &serverMessenger);

    BString url;
    url.SetToFormat("%s/v1beta/models/%s:generateContent?key=%s", baseUrl.String(), modelName, apiKey.String());
    
    // Compiliamo il payload iniettando lo storico memorizzato nel BMessage
    BString payload;
    BuildPayloadFromContext(config, prompt, payload);
    fprintf(stderr, "[GEMINI PLUGIN] Payload JSON generato:\n%s\n", payload.String());
    
    BMallocIO* out = new BMallocIO();
    SyncListener listener;
    BUrl bUrl(url.String(), true);
    
    // Istanziamo il BUrlContext per evitare crash/punti a vtable vuoti
    BUrlContext context;
    
    BUrlRequest* req = BUrlProtocolRoster::MakeRequest(bUrl, out, &listener, &context);
    if (!req) {
        fprintf(stderr, "[GEMINI PLUGIN] ERRORE: BUrlProtocolRoster::MakeRequest ha fallito!\n");
        delete out; 
        snprintf(response_buf, response_len, "{\"error\":\"request creation failed\"}"); 
        return B_ERROR; 
    }

    BHttpRequest* http = dynamic_cast<BHttpRequest*>(req);
    if (http) {
        http->SetMethod(B_HTTP_POST);
        BMallocIO* in = new BMallocIO();
        in->WriteExactly(payload.String(), payload.Length());
        
        // http adotta 'in' e lo libererà da solo con il distruttore di req
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
        delete req;
        delete out; 
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
    bool handled = false;
    BString rawResponse((const char*)buf, len);
    BMessage parsedJson;
    bool parseSuccess = (BJson::Parse(rawResponse.String(), parsedJson) == B_OK);

    if (statusCode == 200 && parseSuccess) {
        BMessage candidates, candidateZero, content, parts, partZero;
        const char* extractedText = nullptr;

        bool found = (parsedJson.FindMessage("candidates", &candidates) == B_OK)
            && (candidates.FindMessage("0", &candidateZero) == B_OK || candidates.FindMessage("msg", 0, &candidateZero) == B_OK)
            && (candidateZero.FindMessage("content", &content) == B_OK)
            && (content.FindMessage("parts", &parts) == B_OK)
            && (parts.FindMessage("0", &partZero) == B_OK || parts.FindMessage("msg", 0, &partZero) == B_OK)
            && (partZero.FindString("text", &extractedText) == B_OK && extractedText != nullptr);

        if (found) {
            size_t textLen = strlen(extractedText);
            size_t copy_len = textLen < response_len - 1 ? textLen : response_len - 1;
            memcpy(response_buf, extractedText, copy_len);
            response_buf[copy_len] = '\0';
            handled = true;
            fprintf(stderr, "[GEMINI PLUGIN] Testo estratto con successo dal JSON.\n");
        }
    } else if (statusCode != 200 && parseSuccess) {
        // Estrazione messaggio d'errore strutturato da Gemini
        BMessage errorObj;
        if (parsedJson.FindMessage("error", &errorObj) == B_OK) {
            const char* errMsg = errorObj.FindString("message");
            if (errMsg) {
                snprintf(response_buf, response_len, "[Errore Gemini %" B_PRId32 "] %s", statusCode, errMsg);
            } else {
                snprintf(response_buf, response_len, "[Errore Gemini %" B_PRId32 "]", statusCode);
            }
            handled = true;
        }
    }

    if (!handled) {
        fprintf(stderr, "[GEMINI PLUGIN] ATTENZIONE: Parsing strutturato fallito, restituzione testo raw.\n");
        size_t copy_len = len < response_len - 1 ? len : response_len - 1;
        memcpy(response_buf, buf, copy_len);
        response_buf[copy_len] = '\0';
    }

    delete req; 
    delete out;
    fprintf(stderr, "[GEMINI PLUGIN] === FINE generate_text_sync ===\n\n");
    return B_OK;//(rc == B_OK && statusCode == 200) ? B_OK : B_ERROR;
}
/*
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
}*/

void ConvertBMessageToGeminiToolsJson(const BMessage* toolsMsg, BString& outJson)
{
    outJson = "[{\"functionDeclarations\":[";

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
        }

        outJson << "}";
        i++;
    }

    outJson << "]}]";
}

static status_t
gemini_stream_thread_func(void* data)
{
    AsyncArgs* args = (AsyncArgs*)data;
    if (!args) return B_BAD_VALUE;

    BString baseUrl;
    BString targetUrl;
    BString tempUrl;

    const char* ctxId = nullptr;
    int32 sessionID = -1;
    if (args->context_copy) {
        args->context_copy->FindString("context_id", &ctxId);
        args->context_copy->FindInt32("session_id", &sessionID);
    }

    if (args->base_url && args->base_url[0] != '\0') {
        baseUrl = args->base_url;
    } else if (args->context_copy && args->context_copy->FindString("base_url", &tempUrl) == B_OK && !tempUrl.IsEmpty()) {
        baseUrl = tempUrl;
    } else {
        baseUrl = DEFAULT_GEMINI_BASE_URL;
    }
    if (baseUrl.EndsWith("/")) baseUrl.Truncate(baseUrl.Length() - 1);

    const char* modelName = (args->model && args->model[0] != '\0') ? args->model : DEFAULT_GEMINI_MODEL;

    BMessage replyTools;
    bool mcpActive = false;

    if (args->server_messenger.IsValid()) {
        BMessage reqTools(MSG_MCP_GET_TOOLS);
        if (ctxId) reqTools.AddString("context_id", ctxId);
        
        if (args->server_messenger.SendMessage(&reqTools, &replyTools) == B_OK) {
            if (replyTools.HasMessage("tool", 0)) {
                mcpActive = true;
                fprintf(stderr, "[GEMINI STREAM WORKER] Strumenti MCP rilevati e attivi.\n");
            }
        }
    }

    if (!mcpActive) {
        goto fallback_to_standard;
    }

    // === MODALITÀ MCP (Loop function calling sincrono) ===
    {
        targetUrl.SetToFormat("%s/v1beta/models/%s:generateContent?key=%s", 
                              baseUrl.String(), modelName, args->api_key ? args->api_key : "");

        bool executionLoop = true;
        while (executionLoop) {
            if (args->server_messenger.IsValid()) {
                BMessage checkAbortMsg('CHAB');
                if (ctxId) checkAbortMsg.AddString("context_id", ctxId);
                
                BMessage abortReply;
                if (args->server_messenger.SendMessage(&checkAbortMsg, &abortReply) == B_OK) {
                    int32 status = B_OK;
                    if (abortReply.FindInt32("status", &status) == B_OK && status == B_CANCELED) {
                        fprintf(stderr, "[GEMINI STREAM WORKER] Rilevata interruzione asincrona dall'utente.\n");
                        if (args->notify_path) {
                            BFile streamFile(args->notify_path, B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
                            if (streamFile.InitCheck() == B_OK) {
                                streamFile.Write("<<STREAM_ABORT>>", 16);
                            }
                        }
                        goto thread_cleanup;
                    }
                }
            }

            BString toolsJson;
            ConvertBMessageToGeminiToolsJson(&replyTools, toolsJson);

            BString payload;
            BuildPayloadFromContext(args->context_copy, nullptr, payload, &toolsJson);

            BMallocIO outNetworkData;
            SyncListener syncListener;
            BUrl bUrl(targetUrl.String(), true);
            BUrlRequest* req = BUrlProtocolRoster::MakeRequest(bUrl, &outNetworkData, &syncListener, NULL);
            if (!req) {
                executionLoop = false;
                break;
            }
            fprintf(stderr, "[GEMINI MCP DEBUG] Target URL: %s\n", targetUrl.String());
			fprintf(stderr, "[GEMINI MCP DEBUG] Payload inviato:\n%s\n", payload.String());

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

            uint32 httpStatusCode = 0;
            if (http) {
                const BHttpResult* result = dynamic_cast<const BHttpResult*>(&http->Result());
                if (result != nullptr)
                    httpStatusCode = result->StatusCode();
            }

            delete req;

            BString rawResponse((const char*)outNetworkData.Buffer(), outNetworkData.BufferLength());
            BMessage parsedJson;
            bool parseOk = (BJson::Parse(rawResponse.String(), parsedJson) == B_OK);

            if (httpStatusCode != 200 || !parseOk) {
                fprintf(stderr, "[GEMINI STREAM WORKER] Errore di rete/HTTP (Status %" B_PRId32 ") o JSON malformato.\n", httpStatusCode);
                DispatchError(args->server_messenger, httpStatusCode, sessionID, ctxId, rawResponse);
                fprintf(stderr, "[GEMINI MCP DEBUG] Risposta HTTP %" B_PRId32 " dal server Gemini:\n%s\n", httpStatusCode, rawResponse.String());
                
                // Segnala errore visibile anche sul notify_path prima di uscire
                BFile streamFile(args->notify_path, B_WRITE_ONLY | B_CREATE_FILE | B_OPEN_AT_END);
                /*if (streamFile.InitCheck() == B_OK) {
                    BString guiError;
                    guiError.SetToFormat("\n[Errore API Gemini (%d)]\n", httpStatusCode);
                    streamFile.Write(guiError.String(), guiError.Length());
                }*/
                if (streamFile.InitCheck() == B_OK) {
                    BString guiError;
                    BMessage errorDetails;
                    const char* apiErrorText = nullptr;
                    if (parseOk && parsedJson.FindMessage("error", &errorDetails) == B_OK) {
                        errorDetails.FindString("message", &apiErrorText);
                    }

                    if (apiErrorText && apiErrorText[0] != '\0') {
                        guiError.SetToFormat("\n[Errore API Gemini (%d): %s]\n", httpStatusCode, apiErrorText);
                    } else {
                        guiError.SetToFormat("\n[Errore API Gemini (%d)]\n", httpStatusCode);
                    }
                    streamFile.Write(guiError.String(), guiError.Length());
                }

                executionLoop = false;
                break;
            }

            // Parsing della risposta del candidato Gemini
            BMessage candidates, candZero, contentMsg, partsMsg, partZero;
            bool hasCandidates = (parsedJson.FindMessage("candidates", &candidates) == B_OK)
                && (candidates.FindMessage("0", &candZero) == B_OK || candidates.FindMessage("msg", 0, &candZero) == B_OK)
                && (candZero.FindMessage("content", &contentMsg) == B_OK)
                && (contentMsg.FindMessage("parts", &partsMsg) == B_OK)
                && (partsMsg.FindMessage("0", &partZero) == B_OK || partsMsg.FindMessage("msg", 0, &partZero) == B_OK);

            if (hasCandidates) {
                BMessage functionCallObj;
                const char* toolName = nullptr;
                const char* textContent = nullptr;

                if (partZero.FindMessage("functionCall", &functionCallObj) == B_OK 
                    && functionCallObj.FindString("name", &toolName) == B_OK) {
                    
                    BMessage argsMsg;
                    functionCallObj.FindMessage("args", &argsMsg);
                    
                    const char* thoughtSig = nullptr;
                    partZero.FindString("thought_signature", &thoughtSig);

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
                                toolResultBuf = "Operazione completata con successo.";
                            }
                        } else {
                            toolResultBuf = "{\"error\":\"Il comando sul server ha restituito una risposta vuota.\"}";
                        }
                    } else {
                        toolResultBuf = "{\"error\":\"Esecuzione dello strumento fallita via IPC BMessenger\"}";
                    }

                    AppendToolCallToContext(args->context_copy, toolName, &argsMsg, thoughtSig);
                    AppendToolResponseToContext(args->context_copy, toolName, toolResultBuf.String());
                } 
                else if (partZero.FindString("text", &textContent) == B_OK && textContent != nullptr) {
                    BFile streamFile(args->notify_path, B_WRITE_ONLY | B_CREATE_FILE | B_OPEN_AT_END);
                    if (streamFile.InitCheck() == B_OK) {
                        streamFile.Write(textContent, strlen(textContent));
                        streamFile.Flush();
                    }
                    executionLoop = false;
                }
            } else {
                fprintf(stderr, "[GEMINI STREAM WORKER] Risposta priva di candidati o non valida.\n");
                DispatchError(args->server_messenger, httpStatusCode, sessionID, ctxId, rawResponse);
                executionLoop = false;
            }
        }
        goto thread_post_actions;
    }

// === MODALITÀ STANDARD (Streaming SSE/Chunked) ===
fallback_to_standard:
{
    targetUrl.SetToFormat("%s/v1beta/models/%s:streamGenerateContent?alt=sse&key=%s", 
                          baseUrl.String(), modelName, args->api_key ? args->api_key : "");

    BString payload;
    BuildPayloadFromContext(args->context_copy, nullptr, payload, nullptr);

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
                fprintf(stderr, "[GEMINI STREAM ERRORE] Status HTTP non valido in streaming: %d\n", result->StatusCode());
                DispatchError(args->server_messenger, result->StatusCode(), sessionID, ctxId, streamTarget.RawResponse());
            }
        }
        delete req;
    }
}

thread_post_actions:
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
                                                  const char* prompt, 
                                                  BMessage* config)
{
    // Cast alla nuova struct di base dei plugin
    //AIPluginHandle* h = (AIPluginHandle*)handle;
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
        fprintf(stderr, "[GEMINI_PLUGIN] Errore: notify_path mancante.\n");
        return B_ERROR;
    }
    
    // Allochiamo con 'new' la struct condivisa AsyncArgs
    AsyncArgs* args = new(std::nothrow) AsyncArgs();
    if (!args) return B_ERROR;
    
    // Inizializzazione pulita senza dupstr_or_null
    args->api_key = dupstr_or_null(apiKey);
    args->model = dupstr_or_null(modelName && modelName[0] ? modelName : DEFAULT_GEMINI_MODEL);
    args->notify_path = dupstr_or_null(notifyPath);
    if (configBaseUrl && configBaseUrl[0] != '\0') {
        args->base_url = dupstr_or_null(configBaseUrl);
    } else {
    	args->base_url = strdup(DEFAULT_GEMINI_BASE_URL);
    }

    args->context_copy = new (std::nothrow) BMessage(*config);
    if (!args->context_copy) {
        delete args;
        return B_NO_MEMORY;
    }
    
    if (prompt && prompt[0] != '\0' && !args->context_copy->HasString("prompt")) {
        args->context_copy->AddString("prompt", prompt);
    }
    
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
        delete args;
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
        baseUrl = DEFAULT_GEMINI_BASE_URL;
    }
    if (baseUrl.EndsWith("/")) baseUrl.Truncate(baseUrl.Length() - 1);

    BString url;
    url.SetToFormat("%s/v1beta/models?key=%s", baseUrl.String(), apiKey ? apiKey : "");

    BMallocIO* out = new BMallocIO();
    if (!out) return B_NO_MEMORY;
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
        fprintf(stderr, "[GEMINI LIST MODELS ERROR] Status HTTP: %" B_PRId32 ", RC: %" B_PRId32 "\n", statusCode, rc);

        if (serverMessenger.IsValid()) {
            BMessage errorReport(MSG_MOD_ERROR);
            errorReport.AddString("plugin_name", "Google Gemini");
            errorReport.AddInt32("http_code", statusCode);

            if (sessionID != -1) errorReport.AddInt32("session_id", sessionID);
            if (ctxId) errorReport.AddString("session_id", ctxId);

            BString rawError((const char*)out->Buffer(), out->BufferLength());
            BMessage parsedJson;
            BMessage errorObj;
            if (rawError.Length() > 0 && BJson::Parse(rawError.String(), parsedJson) == B_OK
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
                    genericErr.SetToFormat("Impossibile recuperare i modelli Gemini. Errore HTTP %" B_PRId32, statusCode);
                    errorReport.AddString("error_message", genericErr.String());
                } else {
                    errorReport.AddString("error_message", "Errore di connessione durante il recupero dei modelli Gemini.");
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
        int32 matchPos = jsonResponse.FindFirst("\"name\": \"models/", currentPos);
        if (matchPos == B_ERROR) matchPos = jsonResponse.FindFirst("\"name\":\"models/", currentPos);
        if (matchPos == B_ERROR) break;

        matchPos = jsonResponse.FindFirst("models/", matchPos) + 7;
        int32 endPos = jsonResponse.FindFirst("\"", matchPos);
        if (endPos == B_ERROR) break;

        BString modelName;
        jsonResponse.CopyInto(modelName, matchPos, endPos - matchPos);

        bool isEmbedding = (modelName.FindFirst("embedding") != B_ERROR);
        bool isAQA = (modelName.FindFirst("aqa") != B_ERROR);

        if (!isEmbedding && !isAQA) {
            if (!first) cleanJson.Append(", ");
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
    return "GeminiPlugin";
}

