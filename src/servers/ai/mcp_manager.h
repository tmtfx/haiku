/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef MCP_MANAGER_H
#define MCP_MANAGER_H

#include "ai_server.h"
#include <SupportDefs.h>
#include <String.h>
#include <Message.h>
#include <List.h>

// Popola la BList con i tool disponibili in base ai permessi
void PopulateMcpTools(BList& mpcManager, uint32 permissions);

// Estrae una stringa da un payload JSON
bool ExtractStringFromJson(const char* json, const char* key, BString& out, bool unescapeControlChars = true);

// Esegue uno specifico strumento locale in sicurezza e ne restituisce il risultato
BString ExecuteLocalTool(ClientSession* session, const char* toolName, const BMessage& arguments);

#ifdef LOVE_IS_IN_THE_AIR
// Invia una richiesta HTTP all'API locale di Lovense Connect
BString SendLovenseHttpRequest(const char* host, int32 port, const char* jsonPayload);
#endif

// Esegue un comando generico di sistema tramite shell
BString RunSystemCommand(const char* command);

#endif // MCP_MANAGER_H
