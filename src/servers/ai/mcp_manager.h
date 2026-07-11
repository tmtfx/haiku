/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef MCP_MANAGER_H
#define MCP_MANAGER_H

#include <SupportDefs.h>
#include <String.h>
#include <Message.h>
#include <List.h>

// Popola la BList con i tool disponibili in base ai permessi
void PopulateMcpTools(BList& mpcManager, uint32 permissions);

// Estrae una stringa da un payload JSON
bool ExtractStringFromJson(const char* json, const char* key, BString& out);

// Esegue uno specifico strumento locale in sicurezza e ne restituisce il risultato
BString ExecuteLocalTool(const char* toolName, const BMessage& arguments);

// Esegue un comando generico di sistema tramite shell
BString RunSystemCommand(const char* command);

#endif // MCP_MANAGER_H
