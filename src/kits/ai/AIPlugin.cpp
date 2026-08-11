/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include <Json.h>
#include <AIPlugin.h>
#include <cstdio>


void extract_json_field(const char* json, const char* key, char* out, size_t out_len) {
    out[0] = '\0';
    if (!json || !key) return;
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\":\"", key);
    const char* p = strstr(json, needle);
    if (!p) return;
    p += strlen(needle);
    const char* q = strchr(p, '"');
    if (!q) return;
    size_t len = q - p;
    if (len >= out_len) len = out_len - 1;
    memcpy(out, p, len);
    out[len] = '\0';
}

void DispatchError(const BMessenger& messenger, int32 httpCode, 
                   int32 sessionID, const char* ctxId, const BString& rawResponse)
{
    if (!messenger.IsValid()) return;

    BMessage errorReport(MSG_LLM_ERROR);
    errorReport.AddString("plugin_name", "OpenAIPlugin");
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
}
