/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
// ai_server.cpp
// AI service daemon skeleton for Haiku.
// Implements a BLooper-based IPC listener using BMessage/BMessenger.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <dlfcn.h>
#include <dirent.h>
#include <unistd.h>
#include <thread>
#include <map>

#include <Looper.h>
#include <Message.h>
#include <Messenger.h>
#include <String.h>
#include <FindDirectory.h>
#include <Path.h>
#include <Application.h>
#include <Directory.h>
#include <Entry.h>
#include <Node.h>
#include <File.h>
#include <Alert.h>
#include <Catalog.h>

#include <os/ai/AIPlugin.h>
#include <AIConfig.h>
#include <AICommands.h>
#include "ai_server.h"
#include "mcp_manager.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "AIServer"

const char* kServerSignature = "application/x-vnd.Haiku-ai_server";
const char* kNewChat = B_TRANSLATE("New chat");

static std::vector<PluginEntry> gPlugins;

static int32 gNextSessionID = 1;
static std::map<int32, ClientSession> gSessions;


static bool _has_shared_object_suffix(const char* name) {
	std::string s(name);
	if (s.size() >= 3 && s.substr(s.size()-3) == ".so") return true;
	if (s.find(".so.") != std::string::npos) return true;
	return false;
}

static void load_plugins(const char* dirpath)
{
	fprintf(stderr, "ai_server: [LOADER] Scanning directory: %s\n", dirpath);
	BDirectory dir(dirpath);
	if (dir.InitCheck() != B_OK) {
		fprintf(stderr, "ai_server: [LOADER] Unable opening directory: %s\n", dirpath);
		return;
	}

	BEntry entry;
	int scannedCount = 0;
	while (dir.GetNextEntry(&entry) == B_OK) {
		BPath path;
		if (entry.GetPath(&path) != B_OK) continue;

		char name[B_FILE_NAME_LENGTH];
		entry.GetName(name);
		scannedCount++;

		if (!entry.IsFile()) continue;

		struct stat st;
		if (entry.GetStat(&st) != B_OK) continue;

		if (!_has_shared_object_suffix(name) && !(st.st_mode & S_IXUSR)) continue;

		// --- Lettura Attributi BFS ---
		BString pType = "local"; // Fallback di sicurezza
		BNode node(&entry);
		if (node.InitCheck() == B_OK) {
			char typeBuffer[32];
			ssize_t bytesRead = node.ReadAttr("AI:plugin_type", B_STRING_TYPE, 0, typeBuffer, sizeof(typeBuffer) - 1);
			if (bytesRead > 0) {
				typeBuffer[bytesRead] = '\0';
				pType = typeBuffer;
				fprintf(stderr, "ai_server: [LOADER] File '%s' has attribute AI:plugin_type = '%s'\n", name, pType.String());
			} else {
				fprintf(stderr, "ai_server: [LOADER] File '%s' WARNING: no attribute AI:plugin_type found. Using fallback: '%s'\n", name, pType.String());
			}
		} else {
			fprintf(stderr, "ai_server: [LOADER] Error BNode InitCheck for file '%s'\n", name);
		}
		// -----------------------------

		void* h = dlopen(path.Path(), RTLD_NOW);
		if (!h) {
			fprintf(stderr, "ai_server: [LOADER] dlopen failed for '%s': %s\n", name, dlerror());
			continue;
		}

		auto init = (ai_plugin_t (*)(void)) dlsym(h, "ai_plugin_init");
		auto fin = (void (*)(ai_plugin_t)) dlsym(h, "ai_plugin_free");
		if (!init || !fin) {
			fprintf(stderr, "ai_server: [LOADER] '%s' missing mandatory symbols (init/free)\n", name);
			dlclose(h);
			continue;
		}

		auto gen_sync = (status_t (*)(ai_plugin_t, const char*, char*, size_t, BMessage*)) dlsym(h, "ai_plugin_generate_text_sync");
		auto gen_async = (status_t (*)(ai_plugin_t, const char*, BMessage*)) dlsym(h, "ai_plugin_generate_text_async");
		auto get_cap = (uint32 (*)(void)) dlsym(h, "ai_plugin_get_capabilities");
		auto list_models = (status_t (*)(const BMessage*, char*, size_t)) dlsym(h, "ai_plugin_list_models");
		//auto set_model = (status_t (*)(ai_plugin_t, const char*)) dlsym(h, "ai_plugin_set_model");

		const char* (*get_name)(void) = (const char* (*)(void))dlsym(h, "get_plugin_name");

		AISettings s;
		LoadAISettings(s);

		// 2. Costruiamo il BMessage completo da passare al plugin nativo
		BMessage configMsg(MSG_SAVE_CONFIG);
		configMsg.AddString("engine", s.engine.String());
		configMsg.AddString("plugin", s.plugin.String());
		configMsg.AddString("model_name", s.model.String()); // Usiamo "model_name" coerente con il contesto
		configMsg.AddString("api_key", s.api_key.String());
		configMsg.AddString("base_url", s.base_url.String());

		// 3. Inizializziamo l'istanza passando il puntatore al BMessage
		ai_plugin_t inst = init();
		if (!inst) {
			fprintf(stderr, "ai_server: [LOADER] Instance initialization failed for '%s'\n", name);
			dlclose(h);
			continue;
		}

		uint32 caps = 0;
		if (get_cap != nullptr) {
			caps = get_cap();
		}

		// Il plugin è valido se espone ALMENO una capacità nel bitmask, OPPURE se permette il listing dei modelli
		if (caps == 0 && !list_models) {
			fprintf(stderr, "ai_server: [LOADER] '%s' is not declaring capacities and is not listing models, dismissed\n", name);
			fin(inst);
			dlclose(h);
			continue;
		}

		PluginEntry e;
		e.dlhandle = h;
		e.instance = inst;
		e.path = path.Path();
		e.type = pType; 
		e.generate_sync = gen_sync;
		e.generate_async = gen_async;
		e.get_capabilities = get_cap; // Aggiornato con il nuovo puntatore
		e.list_models = list_models;
		//e.set_model = set_model;

		if (get_name != nullptr) {
			e.name = get_name();
		} else {
			e.name = path.Leaf();
		}

		// Evita caricamento di duplicati con lo stesso nome
		bool duplicate = false;
		for (const auto& existing : gPlugins) {
			if (existing.name == e.name) {
				duplicate = true;
				break;
			}
		}

		if (duplicate) {
			fprintf(stderr, "ai_server: [LOADER] A plugin with name '%s' is already loaded. Dismissing duplicate '%s'\n", e.name.String(), name);
			fin(inst);
			dlclose(h);
			continue;
		}
		

		
		/*if (s.plugin == e.name) {
			if (e.set_model && s.model.Length() > 0) {
				e.set_model(e.instance, s.model.String());
				fprintf(stderr, "ai_server: [LOADER] Modello predefinito '%s' applicato a %s\n", s.model.String(), e.name.String());
			}
		}*/
		
		gPlugins.push_back(e);
		
		fprintf(stderr, "ai_server: [LOADER] REGISTRATO CORRETTAMENTE: %s [%s]\n", name, pType.String());
	}
	fprintf(stderr, "ai_server: [LOADER] Fine scansione dir. Elementi totali analizzati: %d\n", scannedCount);
}

static PluginEntry* default_plugin()
{
	if (gPlugins.empty()) return nullptr;
	return &gPlugins[0];
}

static BPath get_context_directory() {
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK) {
		path.Append("ai_server/contexts");
		// Crea la cartella se non esiste
		create_directory(path.Path(), 0755);
	}
	return path;
}

static BString get_context_file_path(const char* contextID) {
	BPath path = get_context_directory();
	path.Append(contextID);
	return path.Path();
}

static status_t append_message_to_context(BMessage* context, const char* role, const char* content) {
	BMessage historyMsg;
	status_t rc = context->FindMessage("messages", &historyMsg);
	if (rc != B_OK) {
		historyMsg = BMessage();
	}
	
	// Creiamo il sotto-messaggio nativo per il singolo turno
	BMessage newMsg;
	newMsg.AddString("role", role);
	newMsg.AddString("content", content);
	
	// Lo appendiamo nell'array nativo
	historyMsg.AddMessage("msg", &newMsg);
	
	// Aggiorniamo il messaggio principale (Rimuoviamo il vecchio e mettiamo il nuovo)
	//context->RemoveName("messages");
	//context->AddMessage("messages", &historyMsg);
	context->ReplaceMessage("messages", &historyMsg);
	
	return B_OK;
}


// Salva (o aggiorna) il contesto nativo su disco solo se ci sono messaggi e non operiamo in remoto
status_t save_chat_context(const char* contextID, BMessage* context) {
	BMessage historyMsg;
	bool hasMessages = false;
	if (context->FindMessage("messages", &historyMsg) == B_OK) {
		type_code type;
		int32 count = 0;
		if (historyMsg.GetInfo("msg", &type, &count) == B_OK && count > 0) {
			hasMessages = true;
		}
	}
	
	const char* remoteId = nullptr;
	bool hasRemoteId = (context->FindString("remote_id", &remoteId) == B_OK 
						&& remoteId != nullptr && remoteId[0] != '\0');

	if (!hasMessages && !hasRemoteId) {
		// Contesto ancora vuoto, non inquiniamo il disco
		return B_OK;
	}
	
	BString path = get_context_file_path(contextID);
	BFile file(path.String(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	
	if (file.InitCheck() != B_OK) return file.InitCheck();
	
	return context->Flatten(&file);
}

// Carica il contesto da disco. Se il file non esiste, lo inizializza come nuovo.
status_t load_or_create_chat_context(const char* contextID, BMessage* outContext, 
									 const char* defaultPlugin = "", const char* defaultModel = "") {
	BString path = get_context_file_path(contextID);
	BFile file(path.String(), B_READ_ONLY);
	
	if (file.InitCheck() == B_OK) {
		// Il contesto esiste, lo srotoliamo in memoria
		return outContext->Unflatten(&file);
	}
	
	// Il contesto non esiste: lo inizializziamo con i metadati di base
	outContext->MakeEmpty();
	outContext->AddString("context_id", contextID);
	outContext->AddString("title", kNewChat);
	outContext->AddString("plugin_name", defaultPlugin);
	outContext->AddString("model_name", defaultModel);
	outContext->AddString("remote_id", ""); // Vuoto di default (modalità locale)
	
	// Creiamo un messaggio contenitore vuoto per la cronologia
	BMessage emptyHistory;
	outContext->AddMessage("messages", &emptyHistory);
	
	return save_chat_context(contextID, outContext);
}

static bool check_update_context_title(BMessage* chatContext){
	bool tosave=false;
	BString title;
	chatContext->FindString("title", &title);
	if (title == kNewChat) {
		BMessage historyMsg;
		if (chatContext->FindMessage("messages", &historyMsg) == B_OK) {
			BMessage firstMsg;
			if (historyMsg.FindMessage("msg", 0, &firstMsg) == B_OK) {
				BString content;
				if (firstMsg.FindString("content", &content) == B_OK && !content.IsEmpty()) {
					title = content;
					if (title.Length() > 30) {
						title.Truncate(30);
						title.Append("...");
					}
					chatContext->RemoveName("title");
					chatContext->AddString("title", title.String());
					tosave=true;
				}
			}
		}
	}
	return tosave;
}

class AIServerApp : public BApplication {
public:
	AIServerApp() : BApplication(kServerSignature) {
		fprintf(stderr, "ai_server: BApplication constructor executed.\n");
	}
	
	void ReadyToRun() override
	{
		fprintf(stderr, "ai_server: ReadyToRun() - Avvio scansione dei plugin...\n");
		BPath pth;
		
		if (find_directory(B_USER_NONPACKAGED_ADDONS_DIRECTORY, &pth) == B_OK) {
			fprintf(stderr, "Ricerca Plugins nella cartella utente degli addons\n");
			pth.Append("ai");
			load_plugins(pth.Path());
		}
		if (find_directory(B_SYSTEM_NONPACKAGED_ADDONS_DIRECTORY, &pth) == B_OK) {
			fprintf(stderr, "Ricerca Plugins nella cartella di sistema non impacchettata degli addons\n");
			pth.Append("ai");
			load_plugins(pth.Path());
		}
		if (find_directory(B_SYSTEM_ADDONS_DIRECTORY, &pth) == B_OK) {
			fprintf(stderr, "Ricerca Plugins nella cartella di sistema degli addons\n");
			pth.Append("ai");
			load_plugins(pth.Path());
		}

		fprintf(stderr, "ai_server: Scansione completata. %zu plugin caricati in memoria.\n", gPlugins.size());
	}

	void MessageReceived(BMessage* msg) override
	{
		// Debug generico per ogni messaggio in ingresso
		char whatChars[5] = {
			(char)(msg->what >> 24),
			(char)(msg->what >> 16),
			(char)(msg->what >> 8),
			(char)(msg->what),
			'\0'
		};
		// Sanatizzazione caratteri non stampabili nel 'what'
		for(int i=0; i<4; i++) { if(whatChars[i] < 32 || whatChars[i] > 126) whatChars[i] = '?'; }

		fprintf(stderr, "ai_server: [IPC] Ricevuto messaggio 'what': %s (0x%x)\n", whatChars, msg->what);
		
		switch (msg->what) {
			case MSG_OPEN_SESSION: {
				BString contextID;
				msg->FindString("context_id", &contextID);

				if (contextID.IsEmpty()) {
					bigtime_t time = real_time_clock();
					contextID.SetToFormat("ctx_%ld", time);
				}
				int32 newSessionID = gNextSessionID++;
				ClientSession& session = gSessions[newSessionID];
				session.id = newSessionID;
				session.context_id = contextID;
				session.useCustomAPIKey = false;

				const char* reqPlugin = msg->FindString("plugin");
				const char* reqModel = msg->FindString("model");
				const char* reqKey = msg->FindString("api_key");
				const char* reqBaseUrl = msg->FindString("base_url");
				
				AISettings globalSettings;
				bool availableGlobalSettings = false;
				if (LoadAISettings(globalSettings)) availableGlobalSettings = true;
				if (availableGlobalSettings) session.useRemoteContext = globalSettings.use_remote_context;
				else session.useRemoteContext = false;

				if (reqPlugin && reqModel) {
					session.plugin_name = reqPlugin;
					session.model_name = reqModel;
					if (reqKey && strlen(reqKey) > 0) {
						session.custom_api_key = reqKey;
						session.useCustomAPIKey = true;
					}
				} else {
					if (availableGlobalSettings) {
						session.plugin_name = globalSettings.plugin;
						session.model_name = globalSettings.model;
						session.custom_api_key = globalSettings.api_key;
						session.useRemoteContext = globalSettings.use_remote_context;
					}
				}
				
				if (reqBaseUrl && strlen(reqBaseUrl) > 0) {
					session.base_url = reqBaseUrl;
				} else if (availableGlobalSettings) {
					session.base_url = globalSettings.base_url;
				}

				// Determina useRemoteContext dal context file salvato:
				// Priorità: 1) flag esplicito nel file, 2) remote_id non vuoto, 3) settings globali
				BString savedRemoteIdStr;
				bool hasRemoteId = false;

				BMessage savedCtx;
				if (load_or_create_chat_context(contextID.String(), &savedCtx,
									session.plugin_name.String(),
									session.model_name.String()) == B_OK) {
					// 1. Flag esplicito salvato nel file (sovrascrive tutto)
					bool explicitFlag = false;
					if (savedCtx.FindBool("use_remote_context", &explicitFlag) == B_OK) {
						session.useRemoteContext = explicitFlag;
					}

					// 2. Se c'è un remote_id valido, forzare true indipendentemente
					const char* remoteIdCheck = nullptr;
					if (savedCtx.FindString("remote_id", &remoteIdCheck) == B_OK
							&& remoteIdCheck && remoteIdCheck[0] != '\0') {
						session.useRemoteContext = true;
						savedRemoteIdStr = remoteIdCheck;
						hasRemoteId = true;
					}
				}

				int32 mcpPermissions = 0;
				//if (msg->FindInt32("mcp_permissions", &mcpPermissions) != B_OK) {
				//	if (availableGlobalSettings) {
				//		mcpPermissions = globalSettings.mcp_permissions;
				//	}
				//}
				if (availableGlobalSettings) {
					mcpPermissions = globalSettings.mcp_permissions;
				}

				// 3. (Opzionale) Se il client vuole auto-limitarsi ulteriormente, può farlo,
				// ma non potrà MAI ottenere più permessi di quelli concessi dal server.
				int32 clientRequestedPermissions = 0;
				if (msg->FindInt32("mcp_permissions", &clientRequestedPermissions) == B_OK) {
					mcpPermissions &= clientRequestedPermissions; // Intersezione bit a bit
				}

				session.mcp_permissions = mcpPermissions;

				// Recuperiamo le capabilities del plugin per attivare MCP se supportato
				PluginEntry* p = nullptr;
				for (auto& pe : gPlugins) {
					if (pe.name == session.plugin_name) {
						p = &pe;
						break;
					}
				}

				uint32 caps = 0;
				if (p && p->get_capabilities != nullptr) {
					caps = p->get_capabilities();
				}

				if (caps & AI_CAP_MCP) {
					PopulateMcpTools(session.mpcManager, session.mcp_permissions);
					fprintf(stderr, "ai_server: Plugin '%s' supporta MCP. Popolati %" B_PRId32 " tool con permessi %" B_PRIu32 "\n",
						session.plugin_name.String(), session.mpcManager.CountItems(), session.mcp_permissions);
				} else {
					fprintf(stderr, "ai_server: Plugin '%s' non supporta MCP.\n", session.plugin_name.String());
				}
				
				gSessions[session.id] = session;

				BMessage reply(B_REPLY);
				reply.AddInt32("session_id", newSessionID);
				reply.AddString("context_id", contextID.String());
				if (hasRemoteId) reply.AddString("remote_id",savedRemoteIdStr.String());
				msg->SendReply(&reply);
				break;
			}

			case MSG_CLOSE_SESSION: {
				int32 id = -1;
				if (msg->FindInt32("session_id", &id) == B_OK) {
					auto it = gSessions.find(id);
					if (it != gSessions.end()) {
						for (int32 i = 0; i < it->second.mpcManager.CountItems(); i++) {
							delete (BMessage*)it->second.mpcManager.ItemAt(i);
						}
						it->second.mpcManager.MakeEmpty();
						gSessions.erase(it);
					}
					fprintf(stderr, "ai_server: Chiusa e liberata sessione %d\n", id);
				}
				BMessage reply(B_REPLY);
				reply.AddInt32("status", B_OK);
				msg->SendReply(&reply);
				break;
			}
			
			case MSG_GET_SESSION_INFO: {
				int32 sessionID = -1;
				BString contextID = "ctx_default"; // Fallback speculare a MSG_GEN_SYNC

				msg->FindInt32("session_id", &sessionID);
				
				// 1. Determiniamo il contextID associato alla sessione (se esiste) o alla richiesta
				const char* reqContextID = msg->FindString("context_id");
				
				bool foundInSession = false;
				ClientSession* activeSession = nullptr;
				
				
				if (sessionID != -1 && gSessions.count(sessionID) > 0) {
					activeSession = &gSessions[sessionID];
					foundInSession = true;
					if (activeSession->context_id.Length() > 0) {
						contextID = activeSession->context_id;
					}
				}
    
				if (reqContextID && reqContextID[0] != '\0') {
					contextID = reqContextID;
				}

				// 2. Carichiamo il contesto direttamente da disco usando l'helper esistente
				BMessage chatContext;
				status_t res = load_or_create_chat_context(contextID.String(), &chatContext);
				
				BMessage reply(B_REPLY);
				if (res == B_OK) {
					BString title;
					BString pluginName;
					BString modelName;
					BString remoteId;
					BString baseUrl;
					
					chatContext.FindString("plugin_name", &pluginName);
					chatContext.FindString("model_name", &modelName);
					chatContext.FindString("remote_id", &remoteId);
					
					if (foundInSession && activeSession) {
						pluginName = activeSession->plugin_name;
						modelName = activeSession->model_name;
						baseUrl = activeSession->base_url;
					} else {
						chatContext.FindString("plugin_name", &pluginName);
						chatContext.FindString("model_name", &modelName);
					}

					chatContext.FindString("remote_id", &remoteId);

					if (baseUrl.IsEmpty()) {
						if (chatContext.FindString("base_url", &baseUrl) != B_OK) {
							AISettings globalSettings;
							if (LoadAISettings(globalSettings)) baseUrl = globalSettings.base_url;
						}
					}

					if (check_update_context_title(&chatContext)) {
						save_chat_context(contextID.String(), &chatContext);
					}

					chatContext.FindString("title", &title);

					reply.AddInt32("status", B_OK);
					reply.AddString("context_id", contextID.String());
					reply.AddString("title", title.String());
					reply.AddString("plugin_name", pluginName.String());
					reply.AddString("model_name", modelName.String());
					
					if (!baseUrl.IsEmpty()) {
						reply.AddString("base_url", baseUrl.String());
					}

					if (!remoteId.IsEmpty()) {
						reply.AddString("remote_id", remoteId.String());
					}
				} else {
					reply.AddInt32("status", res);
				}

				msg->SendReply(&reply);
				break;
			}
			case MSG_GET_ALL_SESSIONS: {
				BMessage reply(B_REPLY);
				
				reply.AddInt32("count", static_cast<int32>(gSessions.size()));

				for (const auto& pair : gSessions) {
					const ClientSession& session = pair.second;
					BMessage sessionInfo;

					sessionInfo.AddInt32("session_id", session.id);
					sessionInfo.AddString("context_id", session.context_id.String());
					// I dati in memoria prevalgono sempre sullo stato persistito per le sessioni attive
					sessionInfo.AddString("plugin_name", session.plugin_name.String());
					sessionInfo.AddString("model_name", session.model_name.String());
					
					if (session.base_url.Length() > 0) {
						sessionInfo.AddString("base_url", session.base_url.String());
					}

					BMessage chatContext;
					BString title = kNewChat;
					BString remoteId;
					
					if (load_or_create_chat_context(session.context_id.String(), &chatContext) == B_OK) {
						if (check_update_context_title(&chatContext)) {
							save_chat_context(session.context_id.String(), &chatContext);
						}
						chatContext.FindString("title", &title);
						chatContext.FindString("remote_id", &remoteId);
					}
					
					sessionInfo.AddString("title", title.String());
					if (!remoteId.IsEmpty()) {
						sessionInfo.AddString("remote_id", remoteId.String());
					}

					reply.AddMessage("session", &sessionInfo);
				}

				msg->SendReply(&reply);
				break;
			}
			/*
			case MSG_GEN_ASYNC: {
				const char* prompt = nullptr;
				if (msg->FindString("prompt", &prompt) != B_OK) {
					BMessage r(MSG_AI_ERROR);
					r.AddString("error", "missing prompt");
					msg->SendReply(&r);
					break;
				}

				BMessenger target;
				if (msg->FindMessenger("target", &target) != B_OK) {
					BMessage r(MSG_AI_ERROR);
					r.AddString("error", "missing target messenger");
					msg->SendReply(&r);
					break;
				}

				int32 sessionID = -1;
				msg->FindInt32("session_id", &sessionID);

				if (sessionID != -1 && gSessions.count(sessionID) > 0) {
					gSessions[sessionID].client_target = target;
				}

				PluginEntry* p = nullptr;
				BString pluginName;
				BString modelName;
				BString apiKey;
				BString baseUrl;
				BString contextID = "ctx_default";

				// --- 1. RISOLUZIONE GERARCHICA PARAMETRI ---
				const char* customPlugin = msg->FindString("custom_plugin");
				const char* customModel  = msg->FindString("custom_model");
				const char* customApiKey = msg->FindString("custom_api_key");
				const char* reqContextID = msg->FindString("context_id");

				if (reqContextID && reqContextID[0] != '\0') {
					contextID = reqContextID;
				}

				// Piorità 1: Parametri una-tantum nel messaggio BMessage di richiesta
				if (customPlugin && customModel) {
					pluginName = customPlugin;
					modelName  = customModel;
					apiKey     = customApiKey ? customApiKey : "";

					for (auto& pe : gPlugins) {
						if (pe.name == customPlugin || pe.name.FindFirst(customPlugin) >= 0) {
							p = &pe;
							break;
						}
					}
				} 
				// Priorità 2: Dati estratti dalla ClientSession in memoria
				else if (sessionID != -1 && gSessions.count(sessionID) > 0) {
					ClientSession& session = gSessions[sessionID];
					pluginName = session.plugin_name;
					modelName  = session.model_name;
					apiKey     = session.custom_api_key;
					baseUrl    = session.base_url; // Vince su qualsiasi base_url salvato nel contesto

					if (session.context_id.Length() > 0) {
						contextID = session.context_id;
					}

					for (auto& pe : gPlugins) {
						if (pe.name == session.plugin_name || 
							pe.name.FindFirst(session.plugin_name) >= 0 || 
							session.plugin_name.FindFirst(pe.name) >= 0) {
							p = &pe;
							break;
						}
					}
				}

				if (!p) p = default_plugin();

				if (!p) {
					BMessage r(MSG_AI_ERROR);
					r.AddString("error", "no plugin available");
					msg->SendReply(&r);
					break;
				}

				if (pluginName.IsEmpty()) pluginName = p->name;
				if (modelName.IsEmpty())  modelName  = "gemini-2.5-flash";

				// --- 2. CARICAMENTO CONTESTO DA DISCO ---
				BMessage* chatContext = new BMessage();
				load_or_create_chat_context(contextID.String(), chatContext, pluginName.String(), modelName.String());

				// Priorità 3: Fallback del base_url da File di Contesto (se la Sessione non l'ha fornito)
				if (baseUrl.IsEmpty()) {
					chatContext->FindString("base_url", &baseUrl);
				}

				// Priorità 4: Fallback del base_url da Impostazioni Globali
				if (baseUrl.IsEmpty()) {
					AISettings globalConf;
					if (LoadAISettings(globalConf)) {
						baseUrl = globalConf.base_url;
					}
				}

				// Recupero API Key se vuota
				if (apiKey.IsEmpty()) {
					GetPluginAPIKey(p->name.String(), apiKey);
				}

				// --- 3. AGGIORNAMENTO FORZATO BMESSAGE DI CONTESTO ---
				chatContext->RemoveName("api_key");
				chatContext->AddString("api_key", apiKey.String());

				chatContext->RemoveName("model_name");
				chatContext->AddString("model_name", modelName.String());

				chatContext->RemoveName("plugin_name");
				chatContext->AddString("plugin_name", pluginName.String());

				chatContext->RemoveName("base_url");
				chatContext->AddString("base_url", baseUrl.String());
				
				chatContext->RemoveName("session_id");
				chatContext->AddInt32("session_id", sessionID);

				chatContext->RemoveName("context_id");
				chatContext->AddString("context_id", contextID.String());

				// --- 4. GESTIONE REMOTE CONTEXT & MAPPING LOCALE ---
				bool useRemoteCtxAsync = false;
				if (sessionID != -1 && gSessions.count(sessionID) > 0) {
					useRemoteCtxAsync = gSessions[sessionID].useRemoteContext;
				} else {
					AISettings globalSA;
					if (LoadAISettings(globalSA))
						useRemoteCtxAsync = globalSA.use_remote_context;
				}

				chatContext->RemoveName("use_remote_context");
				chatContext->AddBool("use_remote_context", useRemoteCtxAsync);

				if (!useRemoteCtxAsync) {
					chatContext->RemoveName("remote_id");
					chatContext->AddString("remote_id", "");

					append_message_to_context(chatContext, "user", prompt);
					save_chat_context(contextID.String(), chatContext);
				}

				// --- 5. PERCORSO STREAM E NOTIFICA ACK ---
				char tmpPath[PATH_MAX];
				snprintf(tmpPath, sizeof(tmpPath), "/tmp/ai_stream_%" B_PRId32 "_%lu.tmp", getpid(), (unsigned long)rand());

				chatContext->RemoveName("notify_path");
				chatContext->AddString("notify_path", tmpPath);

				chatContext->RemoveName("server_messenger");
				chatContext->AddMessenger("server_messenger", be_app_messenger);

				// Risposta ACK immediata al chiamante per sbloccarlo
				BMessage ack;
				ack.AddString("status", "ok");
				msg->SendReply(&ack);

				// Invocazione del plugin
				ai_plugin_t instance = p->instance;
				std::string promptCopy = prompt;

				int rc = p->generate_async(instance, promptCopy.c_str(), chatContext);
				if (rc != 0) {
					BMessage err(MSG_AI_ERROR);
					err.AddString("error", "plugin async failed to start");
					target.SendMessage(&err);

					delete chatContext;
					break;
				}

				// --- 6. LAUNCH WATCHER THREAD (MONITORAGGIO STREAM) ---
				std::thread watcher([target, tmpPath = std::string(tmpPath), contextID, chatContext, useRemoteCtxAsync]() mutable {
					FILE* f = NULL;
					size_t lastSize = 0;
					bool done = false;
					BString fullResponseAccumulator("");
					int emptyReads = 0;
					const int maxEmptyReads = 1200; // ~60 secondi di timeout inattività

					while (!done) {
						if (!f) f = fopen(tmpPath.c_str(), "r");
						if (f) {
							fseek(f, 0, SEEK_END);
							size_t sz = (size_t)ftell(f);
							if (sz > lastSize) {
								fseek(f, (long)lastSize, SEEK_SET);
								size_t toRead = sz - lastSize;
								std::vector<char> buf(toRead + 1);
								size_t r = fread(buf.data(), 1, toRead, f);
								buf[r] = '\0';
								lastSize += r;
								emptyReads = 0; // Reset contatore timeout

								BString chunk(buf.data());
								fullResponseAccumulator << chunk;

								// Controlliamo se il marker di fine stream è presente nel buffer totale
								int32 pos = fullResponseAccumulator.FindFirst("<<STREAM_END>>");
								if (pos != B_ERROR) {
									BString finalCleanText;
									fullResponseAccumulator.CopyInto(finalCleanText, 0, pos);

									// Invio eventuale frammento parziale prima del marker
									if (chunk.FindFirst("<<STREAM_END>>") != B_ERROR) {
										BString partChunk;
										chunk.CopyInto(partChunk, 0, chunk.FindFirst("<<STREAM_END>>"));
										if (partChunk.Length() > 0) {
											BMessage out(MSG_AI_RESPONSE);
											out.AddString("partial", partChunk.String());
											out.AddBool("complete", false);
											target.SendMessage(&out);
										}
									}

									// Invio evento completamento
									BMessage fin(MSG_AI_RESPONSE);
									fin.AddString("response", finalCleanText.String());
									fin.AddBool("complete", true);
									fin.AddInt32("status", 0);
									target.SendMessage(&fin);

									// Pulizia metadati volatili/sensibili prima del salvataggio
									chatContext->RemoveName("api_key");
									chatContext->AddString("api_key", "");
									chatContext->RemoveName("notify_path");
									chatContext->RemoveName("use_remote_context");

									// Salvataggio finale integrato su disco
									if (useRemoteCtxAsync) {
										const char* updatedId = nullptr;
										if (chatContext->FindString("remote_id", &updatedId) == B_OK
											&& updatedId && updatedId[0] != '\0') {
											save_chat_context(contextID.String(), chatContext);
										}
									} else {
										check_update_context_title(chatContext);
										append_message_to_context(chatContext, "assistant", finalCleanText.String());
										save_chat_context(contextID.String(), chatContext);
									}

									done = true;
									break;
								} else {
									// Invio chunk parziale ordinario
									BMessage out(MSG_AI_RESPONSE);
									out.AddString("partial", chunk.String());
									out.AddBool("complete", false);
									target.SendMessage(&out);
								}
							} else {
								emptyReads++;
								if (emptyReads > maxEmptyReads) {
									BMessage err(MSG_AI_ERROR);
									err.AddString("error", "stream timeout reached");
									target.SendMessage(&err);
									done = true;
									break;
								}
							}
						}
						snooze(50000); // Poll 50ms
					}

					if (f) fclose(f);
					remove(tmpPath.c_str());

					// Deallocazione sicura della memoria del contesto
					delete chatContext;
				});

				watcher.detach();
				break;
			}*/
			case MSG_GEN_ASYNC: {
				const char* prompt = nullptr;
				if (msg->FindString("prompt", &prompt) != B_OK) {
					BMessage r(MSG_AI_ERROR);
					r.AddString("error", "missing prompt");
					msg->SendReply(&r);
					break;
				}

				BMessenger target;
				if (msg->FindMessenger("target", &target) != B_OK) {
					BMessage r(MSG_AI_ERROR);
					r.AddString("error", "missing target messenger");
					msg->SendReply(&r);
					break;
				}

				int32 sessionID = -1;
				msg->FindInt32("session_id", &sessionID);

				if (sessionID != -1 && gSessions.count(sessionID) > 0) {
					gSessions[sessionID].client_target = target;
					// azzeriamo il flag di abort se rimasto attivo
					gSessions[sessionID].abort_requested = false;
				}

				PluginEntry* p = nullptr;
				BString pluginName;
				BString modelName;
				BString apiKey;
				BString baseUrl;
				BString contextID = "ctx_default";

				// --- 1. RISOLUZIONE GERARCHICA PARAMETRI ---
				const char* customPlugin = msg->FindString("custom_plugin");
				const char* customModel  = msg->FindString("custom_model");
				const char* customApiKey = msg->FindString("custom_api_key");
				const char* reqContextID = msg->FindString("context_id");

				if (reqContextID && reqContextID[0] != '\0') {
					contextID = reqContextID;
				}

				// Priorità 1: Parametri una-tantum nel messaggio BMessage di richiesta
				if (customPlugin && customModel) {
					pluginName = customPlugin;
					modelName  = customModel;
					apiKey     = customApiKey ? customApiKey : "";

					for (auto& pe : gPlugins) {
						if (pe.name == customPlugin || pe.name.FindFirst(customPlugin) >= 0) {
							p = &pe;
							break;
						}
					}
				} 
				// Priorità 2: Dati estratti dalla ClientSession in memoria
				else if (sessionID != -1 && gSessions.count(sessionID) > 0) {
					ClientSession& session = gSessions[sessionID];
					pluginName = session.plugin_name;
					modelName  = session.model_name;
					apiKey     = session.custom_api_key;
					baseUrl    = session.base_url;

					if (session.context_id.Length() > 0) {
						contextID = session.context_id;
					}

					for (auto& pe : gPlugins) {
						if (pe.name == session.plugin_name || 
							pe.name.FindFirst(session.plugin_name) >= 0 || 
							session.plugin_name.FindFirst(pe.name) >= 0) {
							p = &pe;
							break;
						}
					}
				}

				if (!p) p = default_plugin();

				if (!p) {
					BMessage r(MSG_AI_ERROR);
					r.AddString("error", "no plugin available");
					msg->SendReply(&r);
					break;
				}

				if (pluginName.IsEmpty()) pluginName = p->name;
				if (modelName.IsEmpty())  modelName  = "gemini-2.5-flash";

				// --- 2. CARICAMENTO CONTESTO DA DISCO ---
				BMessage* chatContext = new BMessage();
				load_or_create_chat_context(contextID.String(), chatContext, pluginName.String(), modelName.String());

				// Priorità 3: Fallback base_url da File di Contesto
				if (baseUrl.IsEmpty()) {
					chatContext->FindString("base_url", &baseUrl);
				}

				// Priorità 4: Fallback base_url da Impostazioni Globali
				if (baseUrl.IsEmpty()) {
					AISettings globalConf;
					if (LoadAISettings(globalConf)) {
						baseUrl = globalConf.base_url;
					}
				}

				// Recupero API Key se vuota
				if (apiKey.IsEmpty()) {
					GetPluginAPIKey(p->name.String(), apiKey);
				}

				// --- 3. AGGIORNAMENTO FORZATO BMESSAGE DI CONTESTO ---
				chatContext->RemoveName("api_key");
				chatContext->AddString("api_key", apiKey.String());

				chatContext->RemoveName("model_name");
				chatContext->AddString("model_name", modelName.String());

				chatContext->RemoveName("plugin_name");
				chatContext->AddString("plugin_name", pluginName.String());

				chatContext->RemoveName("base_url");
				chatContext->AddString("base_url", baseUrl.String());
				
				chatContext->RemoveName("session_id");
				chatContext->AddInt32("session_id", sessionID);

				chatContext->RemoveName("context_id");
				chatContext->AddString("context_id", contextID.String());

				// --- 4. GESTIONE REMOTE CONTEXT & MAPPING LOCALE ---
				bool useRemoteCtxAsync = false;
				if (sessionID != -1 && gSessions.count(sessionID) > 0) {
					useRemoteCtxAsync = gSessions[sessionID].useRemoteContext;
				} else {
					AISettings globalSA;
					if (LoadAISettings(globalSA))
						useRemoteCtxAsync = globalSA.use_remote_context;
				}

				chatContext->RemoveName("use_remote_context");
				chatContext->AddBool("use_remote_context", useRemoteCtxAsync);

				if (!useRemoteCtxAsync) {
					chatContext->RemoveName("remote_id");
					chatContext->AddString("remote_id", "");

					append_message_to_context(chatContext, "user", prompt);
					save_chat_context(contextID.String(), chatContext);
				}

				// --- 5. PERCORSO STREAM E NOTIFICA ACK ---
				char tmpPath[PATH_MAX];
				snprintf(tmpPath, sizeof(tmpPath), "/tmp/ai_stream_%" B_PRId32 "_%lu.tmp", getpid(), (unsigned long)rand());

				chatContext->RemoveName("notify_path");
				chatContext->AddString("notify_path", tmpPath);

				chatContext->RemoveName("server_messenger");
				chatContext->AddMessenger("server_messenger", be_app_messenger);

				// Risposta ACK immediata al chiamante
				BMessage ack;
				ack.AddString("status", "ok");
				msg->SendReply(&ack);

				// Invocazione del plugin
				ai_plugin_t instance = p->instance;
				std::string promptCopy = prompt;

				int rc = p->generate_async(instance, promptCopy.c_str(), chatContext);
				if (rc != 0) {
					BMessage err(MSG_AI_ERROR);
					err.AddString("error", "plugin async failed to start");
					target.SendMessage(&err);

					delete chatContext;
					break;
				}

				// --- 6. LAUNCH WATCHER THREAD ---
				std::thread watcher([target, tmpPath = std::string(tmpPath), contextID, chatContext, useRemoteCtxAsync]() mutable {
					FILE* f = NULL;
					size_t lastSize = 0;
					bool done = false;
					BString fullResponseAccumulator("");
					int32 sentLength = 0;
					int emptyReads = 0;
					const int maxEmptyReads = 1200; // ~60s timeout

					const char* endToken   = "<<STREAM_END>>";
					const char* abortToken = "<<STREAM_ABORT>>";
					const int32 maxTokenLen = (int32)strlen(abortToken); // 16 byte (il più lungo tra i due)

					while (!done) {
						if (!f) f = fopen(tmpPath.c_str(), "r");
						if (f) {
							fseek(f, 0, SEEK_END);
							size_t sz = (size_t)ftell(f);
							if (sz > lastSize) {
								fseek(f, (long)lastSize, SEEK_SET);
								size_t toRead = sz - lastSize;
								std::vector<char> buf(toRead + 1);
								size_t r = fread(buf.data(), 1, toRead, f);
								buf[r] = '\0';
								lastSize += r;
								emptyReads = 0;

								fullResponseAccumulator << buf.data();

								int32 posAbort = fullResponseAccumulator.FindFirst(abortToken);
								int32 posEnd   = fullResponseAccumulator.FindFirst(endToken);

								// CASO 1: STREAM ABORT
								if (posAbort != B_ERROR) {
									BMessage err(MSG_AI_ERROR);
									err.AddString("error", "stream aborted by provider/plugin");
									target.SendMessage(&err);

									// Non salviamo la risposta nel contesto poiché incompleta/fallita
									done = true;
									break;
								}

								// CASO 2: STREAM END (COMPLETATO CON SUCCESSO)
								if (posEnd != B_ERROR) {
									// Invia l'eventuale pezzetto di testo valido rimasto nel buffer
									if (posEnd > sentLength) {
										BString remainingChunk;
										fullResponseAccumulator.CopyInto(remainingChunk, sentLength, posEnd - sentLength);
										
										BMessage out(MSG_AI_RESPONSE);
										out.AddString("partial", remainingChunk.String());
										out.AddBool("complete", false);
										target.SendMessage(&out);
									}

									// Testo pulito complessivo
									BString finalCleanText;
									fullResponseAccumulator.CopyInto(finalCleanText, 0, posEnd);

									// Notifica di fine stream
									BMessage fin(MSG_AI_RESPONSE);
									fin.AddString("response", finalCleanText.String());
									fin.AddBool("complete", true);
									fin.AddInt32("status", 0);
									target.SendMessage(&fin);

									// Pulizia e salvataggio
									chatContext->RemoveName("api_key");
									chatContext->AddString("api_key", "");
									chatContext->RemoveName("notify_path");
									chatContext->RemoveName("use_remote_context");

									if (useRemoteCtxAsync) {
										const char* updatedId = nullptr;
										if (chatContext->FindString("remote_id", &updatedId) == B_OK
											&& updatedId && updatedId[0] != '\0') {
											save_chat_context(contextID.String(), chatContext);
										}
									} else {
										if (check_update_context_title(chatContext)) {
											BMessage notify(MSG_AI_TITLE_CHANGED);
											notify.AddString("title", chatContext->FindString("title"));
											notify.AddMessenger("messenger",target);
											const BMessenger& me = be_app_messenger;
											me.SendMessage(&notify);
										}
										append_message_to_context(chatContext, "assistant", finalCleanText.String());
										save_chat_context(contextID.String(), chatContext);
									}

									done = true;
									break;
								} 
								
								// CASO 3: STREAM IN CORSO
								// Tratteniamo gli ultimi byte per evitare di emettere frammenti dei token a schermo
								int32 safeLength = fullResponseAccumulator.Length() - (maxTokenLen - 1);
								if (safeLength > sentLength) {
									BString chunkToSend;
									fullResponseAccumulator.CopyInto(chunkToSend, sentLength, safeLength - sentLength);
									sentLength = safeLength;

									BMessage out(MSG_AI_RESPONSE);
									out.AddString("partial", chunkToSend.String());
									out.AddBool("complete", false);
									target.SendMessage(&out);
								}
							} else {
								emptyReads++;
								if (emptyReads > maxEmptyReads) {
									BMessage err(MSG_AI_ERROR);
									err.AddString("error", "stream timeout reached");
									target.SendMessage(&err);
									done = true;
									break;
								}
							}
						}
						snooze(50000); // 50ms
					}

					if (f) fclose(f);
					remove(tmpPath.c_str());
					delete chatContext;
				});

				watcher.detach();
				break;
			}

			case MSG_GEN_SYNC: {
				const char* prompt = nullptr;
				if (msg->FindString("prompt", &prompt) != B_OK) {
					fprintf(stderr, "ai_server: [ERRORE] MSG_GEN_SYNC ricevuto senza prompt!\n");
					BMessage r(MSG_AI_ERROR);
					r.AddString("error", "missing prompt");
					msg->SendReply(&r);
					break;
				}

				int32 sessionID = -1;
				PluginEntry* p = nullptr;
				BString pluginName;
				BString modelName;
				BString apiKey;
				BString baseUrl;
				BString contextID = "ctx_default";
				
				status_t res = msg->FindInt32("session_id", &sessionID);
				
				const char* reqContextID = msg->FindString("context_id");
				if (reqContextID && reqContextID[0] != '\0') {
					contextID = reqContextID;
				}

				if (res == B_OK && gSessions.count(sessionID) > 0) {
					ClientSession& session = gSessions[sessionID];
					pluginName = session.plugin_name;
					modelName = session.model_name;
					apiKey = session.custom_api_key;
					baseUrl = session.base_url;

					if (session.context_id.Length() > 0) {
						contextID = session.context_id;
					}

					// Reset eventuale flag di abort precedente per questa sessione
					session.abort_requested = false;

					for (auto& pe : gPlugins) {
						if (pe.name == session.plugin_name) {
							p = &pe;
							break;
						}
					}
				}

				if (!p) {
					p = default_plugin();
					AISettings globalSettings;
					if (p && LoadAISettings(globalSettings)) {
						pluginName = globalSettings.plugin;
						modelName = globalSettings.model;
						apiKey = globalSettings.api_key;
						if (baseUrl.IsEmpty()) baseUrl = globalSettings.base_url;
					}
				}

				if (!p) {
					BMessage r(MSG_AI_ERROR);
					r.AddString("error", "no plugin available");
					msg->SendReply(&r);
					break;
				}

				if (pluginName.IsEmpty()) pluginName = p->name;
				if (modelName.IsEmpty()) modelName = "gemini-2.5-flash";

				// 1. Carichiamo il contesto da disco
				BMessage chatContext;
				load_or_create_chat_context(contextID.String(), &chatContext, pluginName.String(), modelName.String());

				// Fallback/Integrazione da file di contesto o settings se vuoti
				if (baseUrl.IsEmpty()) {
					if (chatContext.FindString("base_url", &baseUrl) != B_OK) {
						AISettings globalConf;
						if (LoadAISettings(globalConf)) baseUrl = globalConf.base_url;
					}
				}

				// 2. Recupero API Key
				if (apiKey.IsEmpty()) {
					GetPluginAPIKey(p->name.String(), apiKey);
				}

				// 3. Prepariamo i metadati volatili per il plugin
				chatContext.RemoveName("api_key");
				chatContext.AddString("api_key", apiKey.String());
				chatContext.RemoveName("model_name");
				chatContext.AddString("model_name", modelName.String());

				// Impostiamo base_url
				chatContext.RemoveName("base_url");
				chatContext.AddString("base_url", baseUrl.String());

				// Passiamo il context_id al plugin per gli eventuali check sull'abort
				chatContext.RemoveName("context_id");
				chatContext.AddString("context_id", contextID.String());
				
				chatContext.RemoveName("server_messenger");
                chatContext.AddMessenger("server_messenger", BMessenger(this));
				
				if (sessionID != -1) {
                    chatContext.RemoveName("session_id");
                    chatContext.AddInt32("session_id", sessionID);
                }

				// 4. Determiniamo l'autorità del contesto remoto
				bool useRemoteCtx = false;
				if (res == B_OK && gSessions.count(sessionID) > 0) {
					useRemoteCtx = gSessions[sessionID].useRemoteContext;
				} else {
					AISettings globalS;
					if (LoadAISettings(globalS)) {
						useRemoteCtx = globalS.use_remote_context;
					}
				}
				
				chatContext.RemoveName("use_remote_context");
				chatContext.AddBool("use_remote_context", useRemoteCtx);

				// Se la sessione è forzata in LOCALE, puliamo l'eventuale remote_id residuo
				if (!useRemoteCtx) {
					chatContext.RemoveName("remote_id");
					chatContext.AddString("remote_id", "");
					
					// 5. In modalità locale salviamo il prompt nello storico PRIMA della chiamata
					append_message_to_context(&chatContext, "user", prompt);
					save_chat_context(contextID.String(), &chatContext);
				}

				char response[8192] = {0};
				int rc = -1;

				if (p->generate_sync) {
					rc = p->generate_sync(p->instance, prompt, response, sizeof(response), &chatContext);
				}
				
				check_update_context_title(&chatContext);

				BMessage r('RESP');
				if (rc == 0) {
					r.AddString("response", response);
					r.AddInt32("status", 0);

					// Pulizia dati volatili prima del salvataggio definitivo su disco
					chatContext.RemoveName("api_key");
					chatContext.RemoveName("use_remote_context");
					chatContext.RemoveName("server_messenger");
                    chatContext.RemoveName("session_id");

					if (useRemoteCtx) {
						// 6a. Contesto remoto: Il plugin ha generato/aggiornato il remote_id in chatContext
						const char* updatedRemoteId = nullptr;
						if (chatContext.FindString("remote_id", &updatedRemoteId) == B_OK
							&& updatedRemoteId && updatedRemoteId[0] != '\0') {
							save_chat_context(contextID.String(), &chatContext);
						}
					} else {
						// 6b. Contesto locale: salviamo la risposta dell'assistente nello storico
						append_message_to_context(&chatContext, "assistant", response);
						save_chat_context(contextID.String(), &chatContext);
					}
				} else {
					// Se c'è stato un errore e siamo in locale, rimuoviamo l'ultimo messaggio "user" salvato prima
					if (!useRemoteCtx) {
						BMessage messages;
						if (chatContext.FindMessage("messages", &messages) == B_OK) {
							// Ricarichiamo il contesto pulito senza il messaggio fallito
							load_or_create_chat_context(contextID.String(), &chatContext, pluginName.String(), modelName.String());
						}
					}

					r.AddString("error", "plugin generation failed");
					r.AddInt32("status", rc);
				}

				msg->SendReply(&r);
				break;
			}
			case MSG_ABORT_SESSION:
			{
				bool interruptus=false;
				BString contextId = msg->FindString("context_id");
				if (!contextId.IsEmpty()) {
					// Cerchiamo la sessione attiva
					for (auto& pair : gSessions) { 
						if (pair.second.context_id == contextId) {
							pair.second.abort_requested = true; // Attiviamo il Kill Switch!
							interruptus = true;
							fprintf(stderr, "[AI_SERVER] [KILL SWITCH] Richiesta di interruzione ricevuta per la sessione del contesto: %s\n", contextId.String());

							BMessage reply(B_REPLY);
							reply.AddInt32("status", B_OK);
							msg->SendReply(&reply);
							break;
						} else {
							fprintf(stderr, "[AI_SERVER] [KILL SWITCH] ID Contesto della lista %s non corrisponde a quello richiesto %s\n",pair.second.context_id.String(), contextId.String());
						}
					}
				}
				int32 id = -1;
				msg->FindInt32("session_id",&id);
				if ( id > -1 && !interruptus ) {
					for (auto& pair : gSessions) { 
						if (pair.second.id == id) {
							pair.second.abort_requested = true; // Attiviamo il Kill Switch!
							interruptus = true;
							fprintf(stderr, "[AI_SERVER] [KILL SWITCH] Richiesta di interruzione ricevuta per la sessione: %d\n", id);

							BMessage reply(B_REPLY);
							reply.AddInt32("status", B_OK);
							msg->SendReply(&reply);
							break;
						} else {
							fprintf(stderr, "[AI_SERVER] [KILL SWITCH] Sessione in lista %d non corrisponde alla sessione richiesta %d\n", pair.second.id, id);
						}
					}
				}
				if (interruptus) {
					fprintf(stderr, "[AI_SERVER] [KILL SWITCH] Richiesta di interruzione inserita nella sessione\n");
				} else {
					fprintf(stderr, "[AI_SERVER] [KILL SWITCH] Nessuna richiesta inviata, sessione non trovata\n");
				}
				break;
			}
			case MSG_CHECK_ABORT:
			{
				BString contextId = msg->FindString("context_id");
				int32 sessionId = -1;
				msg->FindInt32("session_id", &sessionId);

				BMessage reply(B_REPLY);
				reply.AddInt32("status", B_OK);

				for (auto& pair : gSessions) { 
					bool matchContext = (!contextId.IsEmpty() && pair.second.context_id == contextId);
					bool matchSession = (sessionId > -1 && pair.second.id == sessionId);

					if (matchContext || matchSession) {
						if (pair.second.abort_requested) {
							reply.AddInt32("status", B_CANCELED);
						}
						break;
					}
				}

				msg->SendReply(&reply);
				break;
			}
			case MSG_EXECUTE_TOOL:
			{
				BString contextId = msg->FindString("context_id");
				BString toolName = msg->FindString("name");
				if (toolName.IsEmpty()) {
					toolName = msg->FindString("tool_name");
				}

				BString argsJson = msg->FindString("args");
				fprintf(stderr, "[AI_SERVER] Richiesta esecuzione tool '%s'\n", toolName.String());

				BMessage reply(B_REPLY);
				status_t resultStatus = B_ERROR;
				BString resultOutput;

				// 1. Recupero della sessione attiva
				ClientSession* session = nullptr;
				for (auto& pair : gSessions) { 
					if (pair.second.context_id == contextId) {
						session = &pair.second;
						break;
					}
				}

				if (session == nullptr) {
					reply.AddInt32("status", B_ENTRY_NOT_FOUND);
					reply.AddString("result", "{\"error\":\"Sessione non trovata\"}");
					msg->SendReply(&reply);
					break;
				}
				
				// =========================================================================
				// === INTEGRATIVE KILL SWITCH: INTERRUZIONE TRA UN COMANDO E L'ALTRO ===
				// =========================================================================
				if (session->abort_requested) {
					//session->abort_requested = false; // Resettiamo il flag per le prossime chat // no lo resettiamo alla prossima richiesta

					fprintf(stderr, "[AI_SERVER] [KILL SWITCH] Loop interrotto dall'utente prima del tool '%s'!\n", toolName.String());

					// Ritorniamo un errore specifico (B_CANCELED / Canceled)
					reply.AddInt32("status", B_CANCELED);
		
					// Risposta JSON che dice a Gemini che l'operazione è stata cancellata
					reply.AddString("result", "{\"error\":\"Interrotto: L'utente ha annullato l'esecuzione dei comandi in background.\"}");
					msg->SendReply(&reply);
					break; 
				}
				// =========================================================================

				// 2. CONTROLLO DI SICUREZZA: Il tool è registrato (quindi autorizzato dalla Preflet)?
				BMessage* foundTool = nullptr;
				int32 toolCount = session->mpcManager.CountItems();
				for (int32 i = 0; i < toolCount; i++) {
					BMessage* tool = (BMessage*)session->mpcManager.ItemAt(i);
					if (tool && tool->FindString("name") == toolName) {
						foundTool = tool;
						break;
					}
				}

				if (foundTool == nullptr) {
					reply.AddInt32("status", B_NAME_NOT_FOUND);
					reply.AddString("result", "{\"error\":\"Tool non consentito dalle impostazioni di sicurezza (Preflet)\"}");
					msg->SendReply(&reply);
					break;
				}

				// 3. ESTRAZIONE ROBUSTA DEGLI ARGOMENTI (Normalizzazione per l'esecutore)
                BMessage arguments;
                if (msg->FindMessage("arguments", &arguments) != B_OK) {
                    // FALLBACK RETROCOMPATIBILE: Se il client invia ancora la stringa JSON grezza
                    if (!argsJson.IsEmpty()) {
                        BString path, cmd, text, title, content, action, name, value, pattern;
                        
                        // 1. I percorsi e i comandi/pattern non devono MAI interpretare i caratteri di controllo (SEMPRE false)
                        if (ExtractStringFromJson(argsJson.String(), "path", path, false) || 
                            ExtractStringFromJson(argsJson.String(), "directory", path, false)) {
                            arguments.AddString("path", path);
                        }
                        if (ExtractStringFromJson(argsJson.String(), "cmd", cmd, false) || 
                            ExtractStringFromJson(argsJson.String(), "command", cmd, false)) {
                            arguments.AddString("cmd", cmd);
                        }
                        
                        // === QUI AGGIUNGI IL SUPPORTO A "query" NEL FALLBACK JSON ===
                        if (ExtractStringFromJson(argsJson.String(), "pattern", pattern, false) ||
                            ExtractStringFromJson(argsJson.String(), "query", pattern, false)) {
                            arguments.AddString("pattern", pattern);
                        }

                        // 2. Gestione intelligente per la scrittura di file o attributi BFS
                        bool preserveRawData = (toolName == "create_file" || toolName == "manage_attribute");
                        bool unescapeContent = !preserveRawData;

                        if (ExtractStringFromJson(argsJson.String(), "content", content, unescapeContent)) {
                            arguments.AddString("content", content);
                        }
                        if (ExtractStringFromJson(argsJson.String(), "value", value, unescapeContent)) {
                            arguments.AddString("value", value);
                        }

                        // 3. Campi testuali generici -> interpretazione attiva (SEMPRE true)
                        if (ExtractStringFromJson(argsJson.String(), "text", text, true))       arguments.AddString("text", text);
                        if (ExtractStringFromJson(argsJson.String(), "title", title, true))     arguments.AddString("title", title);
                        if (ExtractStringFromJson(argsJson.String(), "action", action, true))   arguments.AddString("action", action);
                        if (ExtractStringFromJson(argsJson.String(), "name", name, true))       arguments.AddString("name", name);
                    }
                } else {
                    // ECCELLENTE: Il plugin ha inviato direttamente il BMessage analizzato in sicurezza.
                    // I dati sono già perfetti in memoria. Applichiamo solo le normalizzazioni di naming dell'LLM.
                    if (!arguments.HasString("path") && arguments.HasString("directory")) {
                        arguments.AddString("path", arguments.FindString("directory"));
                    }
                    if (!arguments.HasString("cmd") && arguments.HasString("command")) {
                        arguments.AddString("cmd", arguments.FindString("command"));
                    }
                    
                    // === QUI AGGIUNGI IL CONTROLLO NEL RAMO BMESSAGE DIRETTI ===
                    if (!arguments.HasString("pattern") && arguments.HasString("query")) {
                        arguments.AddString("pattern", arguments.FindString("query"));
                    }
                }

				// --- NOTIFICA AL CLIENT DELL'ESECUZIONE IN TEMPO REALE ---
				if (session && session->client_target.IsValid()) {
					BString notificationText;
					if (toolName == "run_terminal_command") {
						const char* cmdToRun = arguments.FindString("cmd");
						notificationText.SetToFormat("\n⚙️ [Esecuzione comando terminale: %s]\n", cmdToRun ? cmdToRun : "");
					} else if (toolName == "create_file") {
						const char* filePath = arguments.FindString("path");
						notificationText.SetToFormat("\n📝 [Creazione file: %s]\n", filePath ? filePath : "");
					} else if (toolName == "make_directory") {
						const char* filePath = arguments.FindString("path");
						notificationText.SetToFormat("\n📁 [Creazione cartella: %s]\n", filePath ? filePath : "");
					} else if (toolName == "delete_file") {
						const char* filePath = arguments.FindString("path");
						notificationText.SetToFormat("\n🗑️ [Eliminazione: %s]\n", filePath ? filePath : "");
					} else if (toolName == "open_document") {
						const char* filePath = arguments.FindString("path");
						notificationText.SetToFormat("\n🚀 [Apertura documento/app: %s]\n", filePath ? filePath : "");
					} else if (toolName == "show_alert_dialog") {
						const char* text = arguments.FindString("text");
						notificationText.SetToFormat("\n💬 [Mostra avviso: %s]\n", text ? text : "");
					} else if (toolName == "manage_attribute") {
						const char* action = arguments.FindString("action");
						const char* filePath = arguments.FindString("path");
						const char* attrName = arguments.FindString("name");
						notificationText.SetToFormat("\n🏷️ [Gestione attributo BFS '%s' (%s) su: %s]\n", 
							attrName ? attrName : "N/A", action ? action : "read", filePath ? filePath : "N/A");
					} else if (toolName == "read_file") {
						const char* filePath = arguments.FindString("path");
						notificationText.SetToFormat("\n📖 [Lettura file: %s]\n", filePath ? filePath : "");
					} else if (toolName == "list_directory") {
						const char* filePath = arguments.FindString("path");
						notificationText.SetToFormat("\n📂 [Elenco cartella: %s]\n", filePath ? filePath : "");
					} else if (toolName == "search_text") {
						const char* pattern = arguments.FindString("pattern");
						const char* filePath = arguments.FindString("path");
						notificationText.SetToFormat("\n🔍 [Ricerca pattern '%s' in: %s]\n", pattern ? pattern : "", filePath ? filePath : "");
					} else if (toolName == "get_system_stats") {
						notificationText = "\n📊 [Lettura statistiche di sistema]\n";
					} else {
						notificationText.SetToFormat("\n⚙️ [Esecuzione strumento: %s]\n", toolName.String());
					}

					if (!notificationText.IsEmpty()) {
						BMessage notifyMsg(MSG_AI_RESPONSE);
						notifyMsg.AddString("partial", notificationText.String());
						notifyMsg.AddBool("complete", false);
						session->client_target.SendMessage(&notifyMsg);
					}
				}

				// 4. DELEGA ALL'ESECUTORE CENTRALE (mcp_manager.cpp)
				fprintf(stderr, "[AI_SERVER] Sicurezza superata. Delegando esecuzione a ExecuteLocalTool per: '%s'\n", toolName.String());
				resultOutput = ExecuteLocalTool(session, toolName.String(), arguments);
				resultStatus = B_OK;

				// 5. Risposta al plugin
				reply.AddInt32("status", resultStatus);
				reply.AddString("result", resultOutput);
				msg->SendReply(&reply);
				break;
			}
			case MSG_MCP_GET_TOOLS:
			{
				// Estraiamo il context_id per capire quale sessione sta chiedendo i tool
				BString contextId = msg->FindString("context_id");
				fprintf(stderr, "[AI_SERVER] Ricevuta richiesta MSG_MCP_GET_TOOLS (Contesto: %s).\n", contextId.String());
				
				// Inizializziamo la risposta specificando che si tratta di un B_REPLY di sistema
				BMessage reply(B_REPLY);
				
				// 1. Cerchiamo la ClientSession corretta nel server
				ClientSession* session = nullptr;
				for (auto& pair : gSessions) {
					if (pair.second.context_id == contextId) {
						session = &pair.second;
						break;
					}
				}
				
				// 2. Se la sessione esiste, leggiamo i tool dalla sua mpcManager (BList)
				if (session != nullptr) {
					int32 toolCount = session->mpcManager.CountItems();
					fprintf(stderr, "[AI_SERVER] Trovata sessione. Recupero %" B_PRId32 " tool da mpcManager.\n", toolCount);
					
					for (int32 i = 0; i < toolCount; i++) {
						// Recuperiamo il puntatore generico dalla BList e facciamo il cast a BMessage*
						BMessage* tool = (BMessage*)session->mpcManager.ItemAt(i);
						if (tool != nullptr) {
							// Aggiungiamo il tool alla risposta radice con la chiave "tool"
							// AddMessage fa una copia profonda del messaggio, ottima per l'IPC
							reply.AddMessage("tool", tool);
						}
					}
				} else {
					fprintf(stderr, "[AI_SERVER] Avviso: Sessione '%s' non trovata. Rispondo con zero tool.\n", contextId.String());
				}

				// Invio sincrono immediato al plugin
				status_t err = msg->SendReply(&reply);
				if (err != B_OK) {
					fprintf(stderr, "[AI_SERVER] Errore critico SendReply: %s\n", strerror(err));
				}
				break;
			}
			case MSG_SET_MCP_PERMISSIONS: {
				int32 sessionID = -1;
				uint32 permissions = 0;
				msg->FindInt32("session_id", &sessionID);
				msg->FindUInt32("permissions", &permissions);

				BMessage reply(B_REPLY);
				status_t status = B_BAD_VALUE;
				if (sessionID != -1 && gSessions.count(sessionID) > 0) {
					ClientSession& session = gSessions[sessionID];
					
					// Sicurezza: i permessi impostati dall'app non possono superare quelli globali della Preflet
					//AISettings globalSettings;
					//if (LoadAISettings(globalSettings)) {
					//	permissions &= globalSettings.mcp_permissions;
					//}
					
					session.mcp_permissions = permissions;

					// Svuotiamo i vecchi tool memorizzati in mpcManager
					for (int32 i = 0; i < session.mpcManager.CountItems(); i++) {
						delete (BMessage*)session.mpcManager.ItemAt(i);
					}
					session.mpcManager.MakeEmpty();

					// Ripopoliamo se il plugin supporta MCP
					PluginEntry* p = nullptr;
					for (auto& pe : gPlugins) {
						if (pe.name == session.plugin_name) {
							p = &pe;
							break;
						}
					}

					uint32 caps = 0;
					if (p && p->get_capabilities != nullptr) {
						caps = p->get_capabilities();
					}

					if (caps & AI_CAP_MCP) {
						PopulateMcpTools(session.mpcManager, session.mcp_permissions);
						fprintf(stderr, "ai_server: Permessi MCP aggiornati dinamicamente per sessione %" B_PRId32 ". Popolati %" B_PRId32 " tool con permessi %" B_PRIu32 "\n",
							sessionID, session.mpcManager.CountItems(), session.mcp_permissions);
					} else {
						fprintf(stderr, "ai_server: Permessi MCP aggiornati per sessione %" B_PRId32 " ma il plugin '%s' non supporta MCP.\n",
							sessionID, session.plugin_name.String());
					}
					status = B_OK;
				}
				reply.AddInt32("status", status);
				msg->SendReply(&reply);
				break;
			}
			case MSG_GET_MCP_PERMISSIONS: {
				int32 sessionID = -1;
				msg->FindInt32("session_id", &sessionID);

				BMessage reply(B_REPLY);
				uint32 permissions = 0;
				status_t status = B_BAD_VALUE;
				if (sessionID != -1 && gSessions.count(sessionID) > 0) {
					permissions = gSessions[sessionID].mcp_permissions;
					status = B_OK;
				}
				reply.AddInt32("status", status);
				reply.AddUInt32("permissions", permissions);
				msg->SendReply(&reply);
				break;
			}
			case MSG_SET_MODEL: {
				// Questo case ora serve solo se l'applicazione vuole sovrascrivere dinamicamente 
				// il modello memorizzato nella *Sessione* del server, senza toccare lo stato del plugin.
				const char* model = nullptr;
				int32 sessionID = -1;
				
				if (msg->FindString("model", &model) != B_OK) {
					BMessage r(MSG_AI_ERROR);
					r.AddString("error", "missing model name");
					msg->SendReply(&r);
					break;
				}
				
				msg->FindInt32("session_id", &sessionID);
				BMessage r('SETR');
				
				if (gSessions.count(sessionID) > 0) {
					gSessions[sessionID].model_name = model;
					r.AddInt32("status", 0);
					fprintf(stderr, "ai_server: Modificato modello in sessione %d in: %s\n", sessionID, model);
				} else {
					r.AddInt32("status", -1);
				}
				msg->SendReply(&r);
				break;
			}
			case MSG_PING: {
				BMessage reply('PONG');
				reply.AddString("status", "ok");
				msg->SendReply(&reply);
				break;
			}
			 
			case MSG_LIST: {
				BString requestedType = "all";
				msg->FindString("requested_type", &requestedType);
				// Tratta stringa vuota o sconosciuta come "all"
				if (requestedType.IsEmpty())
					requestedType = "all";

				BMessage reply('PLUG');
				BMessage pluginList;

				for (const auto& p : gPlugins) {
					BPath pth(p.path.c_str());
					if (requestedType == "all" || p.type == requestedType) {
						BMessage pluginEntry;
						pluginEntry.AddString("plugin_name", pth.Leaf());
						pluginEntry.AddString("plugin_type", p.type);
						pluginList.AddMessage("plugin", &pluginEntry);
					}
				}

				reply.AddMessage("plugins", &pluginList);
				msg->SendReply(&reply);
				break;
			}
			case MSG_GET_MODELS: { // Get Models per un singolo plugin
				BString pluginName;
				if (msg->FindString("plugin_name", &pluginName) != B_OK) {
					BMessage reply('ERR!');
					msg->SendReply(&reply);
					break;
				}
				
				
				BMessage reply('MDBK'); // Models Back
				char buffer[16384] = "[]"; // Aumentato a 16KB: le liste dei modelli (es. Ollama) possono essere molto lunghe!

				// Recuperiamo la chiave dal messaggio
				BString apiKey;
				status_t ret = msg->FindString("apiKey", &apiKey);
				if (ret != B_OK) {
					// Altrimenti recuperiamo la chiave reale per questo plugin dal KeyStore
					GetPluginAPIKey(pluginName.String(), apiKey);
				}
				
				BString baseUrl;
				// 2. Risoluzione di base_url
				ret = msg->FindString("base_url",&baseUrl);

				// Fallback sulle impostazioni salvate se non passati esplicitamente nel messaggio
				if (ret!=B_OK) {
					AISettings s;
					if (LoadAISettings(s) && s.plugin == pluginName) {
						baseUrl = s.base_url;
					}
				}

				// 3. Componiamo il BMessage nativo di configurazione ('AISC') per il plugin
				BMessage configMsg(MSG_SAVE_CONFIG);
				configMsg.AddString("api_key", apiKey.String());
				configMsg.AddString("base_url", baseUrl.String());
				configMsg.AddString("plugin", pluginName.String());

				// 4. Cerchiamo il plugin specifico in memoria
				for (const auto& p : gPlugins) {
					BPath pth(p.path.c_str());
					if (pluginName == pth.Leaf() || pluginName == p.name) {
						if (p.list_models) {
							fprintf(stderr, "[DEBUG SERVER] Richiesta modelli per '%s' (url: '%s')...\n", 
									p.name.String(), baseUrl.String());
							
							// 5. Passiamo il BMessage di configurazione e il buffer di destinazione
							if (p.list_models(&configMsg, buffer, sizeof(buffer)) != 0) {
								strcpy(buffer, "[]");
							}
						}
						break;
					}
				}

				reply.AddString("plugin_models", buffer);
				msg->SendReply(&reply);
				break;
			}
			case MSG_RELOAD: {
				AISettings s;
				int applied = 0;

				// 1. Carichiamo le impostazioni globali aggiornate da disco
				if (LoadAISettings(s)) {
					BString apiKey;
					GetPluginAPIKey(s.plugin.String(), apiKey);

					// 2. Aggiorniamo le sessioni attive che dipendono dalle impostazioni globali
					for (auto& pair : gSessions) {
						ClientSession& session = pair.second;

						// Se la sessione non usava una API Key/configurazione totalmente customizzata dall'utente
						if (!session.useCustomAPIKey) {
							// Aggiorniamo la struttura in memoria
							session.plugin_name = s.plugin;
							session.model_name = s.model;
							session.custom_api_key = apiKey;
							session.base_url = s.base_url;

							// Aggiorniamo anche il contesto persistente (.chat) su disco
							BMessage chatContext;
							if (load_or_create_chat_context(session.context_id.String(), &chatContext) == B_OK) {
								chatContext.RemoveName("plugin_name");
								chatContext.AddString("plugin_name", s.plugin.String());

								chatContext.RemoveName("model_name");
								chatContext.AddString("model_name", s.model.String());

								chatContext.RemoveName("base_url");
								chatContext.AddString("base_url", s.base_url.String());

								save_chat_context(session.context_id.String(), &chatContext);
							}
							applied++;
						}
					}
				}

				BMessage r('RLOD');
				r.AddInt32("applied", applied);
				msg->SendReply(&r);
				break;
			}
			case MSG_GET_CAPABILITIES: {
				fprintf(stderr, "ai_server: [IPC] Ricevuto MSG_GET_CAPABILITIES\n");
				
				BString pluginName;
				uint32 caps = 0;
				BMessage reply('RCAP'); // Reply Capabilities

				if (msg->FindString("plugin_name", &pluginName) == B_OK) {
					// Cerchiamo il plugin nel vettore globale gPlugins
					bool found = false;
					for (const auto& plugin : gPlugins) {
						if (plugin.name == pluginName) {
							found = true;
							// Se il plugin espone la funzione, la chiamiamo, altrimenti lasciamo 0
							if (plugin.get_capabilities != nullptr) {
								caps = plugin.get_capabilities();
								fprintf(stderr, "ai_server: Capabilities per '%s' recuperate: %" B_PRIu32 "\n", 
										pluginName.String(), caps);
							} else {
								fprintf(stderr, "ai_server: Il plugin '%s' non implementa get_capabilities, ritorno 0\n", 
										pluginName.String());
							}
							break;
						}
					}
					if (!found) {
						fprintf(stderr, "ai_server: [ERRORE] Plugin '%s' non trovato nel sistema\n", pluginName.String());
					}
				} else {
					fprintf(stderr, "ai_server: [ERRORE] MSG_GET_CAPABILITIES malformato (manca plugin_name)\n");
				}

				// Inviamo la risposta al client (paro paro il uint32 inserito nel messaggio)
				reply.AddInt32("capabilities", (int32)caps);
				msg->SendReply(&reply);
				break;
			}
			case MSG_GET_REMOTE_CTX_ID: {
				BString contextID;
				msg->FindString("context_id", &contextID);

				// Usiamo un `what-if` code (tipo 'RCXI' o B_REPLY, mantengo il tuo 'RCXI')
				BMessage reply('RCXI');
				BString foundRemoteId = ""; // Fallback standard predefinito

				if (!contextID.IsEmpty()) {
					BMessage ctx;
					if (load_or_create_chat_context(contextID.String(), &ctx, "", "") == B_OK) {
						const char* remoteIdCheck = nullptr;
						if (ctx.FindString("remote_id", &remoteIdCheck) == B_OK && remoteIdCheck) {
							foundRemoteId = remoteIdCheck;
						}
					}
				}
	
				// Inseriamo sempre la stringa (sarà valida o vuota, evitando ambiguità al client)
				reply.AddString("remote_id", foundRemoteId.String());
				msg->SendReply(&reply);
				break;
			}
			case MSG_SET_REMOTE_CTX: {
				int32 sessionID = -1;
				bool enable = false;
				msg->FindInt32("session_id", &sessionID);
				msg->FindBool("enable", &enable);

				BMessage reply(B_REPLY);
				if (sessionID != -1 && gSessions.count(sessionID) > 0) {
					ClientSession& session = gSessions[sessionID];
					session.useRemoteContext = enable;

					// COERENZA DISCO-MEMORIA: 
					// Se disabilitiamo il contesto remoto, svuotiamo il remote_id anche dal file di contesto,
					// altrimenti al prossimo riavvio/caricamento verrebbe riabilitato automaticamente!
					if (!enable) {
						BMessage ctx;
						if (load_or_create_chat_context(session.context_id.String(), &ctx) == B_OK) {
							ctx.RemoveName("remote_id");
							ctx.AddString("remote_id", "");
							// Salva esplicitamente il flag false così alla riapertura
							// non viene sovrascritto dalle settings globali
							ctx.RemoveName("use_remote_context");
							ctx.AddBool("use_remote_context", false);
							save_chat_context(session.context_id.String(), &ctx);
						}
					}

					reply.AddInt32("status", B_OK);
				} else {
					reply.AddInt32("status", B_BAD_VALUE);
				}
	
				msg->SendReply(&reply);
				break;
			}
			case MSG_SET_SYSTEM_PROMPT: {
				int32 sessionID = -1;
				BString contextID;
				BString systemPrompt;
				
				msg->FindInt32("session_id", &sessionID);
				msg->FindString("context_id", &contextID);
				msg->FindString("system_prompt", &systemPrompt);
				
				if (contextID.IsEmpty() && sessionID != -1 && gSessions.count(sessionID) > 0) {
					contextID = gSessions[sessionID].context_id;
				}
				
				BMessage reply(B_REPLY);
				if (!contextID.IsEmpty()) {
					BMessage ctx;
					if (load_or_create_chat_context(contextID.String(), &ctx) == B_OK) {
						ctx.RemoveName("system_prompt");
						if (systemPrompt.Length() > 0) {
							ctx.AddString("system_prompt", systemPrompt.String());
						}
						status_t saveErr = save_chat_context(contextID.String(), &ctx);
						reply.AddInt32("status", saveErr);
					} else {
						reply.AddInt32("status", B_ERROR);
					}
				} else {
					reply.AddInt32("status", B_BAD_VALUE);
				}
				msg->SendReply(&reply);
				break;
			}
			case MSG_GET_SYSTEM_PROMPT: {
				int32 sessionID = -1;
				BString contextID;
				
				msg->FindInt32("session_id", &sessionID);
				msg->FindString("context_id", &contextID);
				
				if (contextID.IsEmpty() && sessionID != -1 && gSessions.count(sessionID) > 0) {
					contextID = gSessions[sessionID].context_id;
				}
				
				BMessage reply(B_REPLY);
				BString systemPrompt = "";
				if (!contextID.IsEmpty()) {
					BMessage ctx;
					if (load_or_create_chat_context(contextID.String(), &ctx) == B_OK) {
						const char* sys = nullptr;
						if (ctx.FindString("system_prompt", &sys) == B_OK && sys) {
							systemPrompt = sys;
						}
					}
				}
				reply.AddString("system_prompt", systemPrompt.String());
				msg->SendReply(&reply);
				break;
			}
			case MSG_LLM_ERROR: {
				int32 sessionID = -1;
				if (msg->FindInt32("session_id", &sessionID) == B_OK && sessionID != -1) {
					if (gSessions.count(sessionID) > 0) {
						BMessenger& clientTarget = gSessions[sessionID].client_target;
						if (clientTarget.IsValid()) {
							BMessage errToClient(MSG_AI_ERROR);
							
							const char* errMsg = msg->FindString("error_message");
							int32 httpCode = 0;
							msg->FindInt32("http_code", &httpCode);
							
							errToClient.AddInt32("http_code", httpCode);
							errToClient.AddString("error", errMsg ? errMsg : "Errore generico dal backend LLM");
							
							clientTarget.SendMessage(&errToClient);
						}
					}
				}
				break;
			}
			case MSG_MOD_ERROR: {
				const char* pluginName = msg->FindString("plugin_name");
				const char* errMsg = msg->FindString("error_message");
				int32 httpCode = 0;
				msg->FindInt32("http_code", &httpCode);

				BString alertText;
				alertText.SetToFormat("Errore durante il caricamento dei modelli (%s):\n\n%s\n(HTTP %" B_PRId32 ")",
									  pluginName ? pluginName : "Plugin",
									  errMsg ? errMsg : "Errore sconosciuto",
									  httpCode);

				BAlert* alert = new BAlert("Modelli AI", alertText.String(), "OK", nullptr, nullptr,
										   B_WIDTH_AS_USUAL, B_STOP_ALERT);
				alert->Go(nullptr); // Asincrono, non blocca il server
				break;
			}
			case MSG_AI_TITLE_CHANGED: // Update Title
			{
				const char* title = nullptr;
				if (msg->FindString("title", &title) != B_OK || !title)
					break;
				
				BMessenger TargetMessenger;
				msg->FindMessenger("messenger",&TargetMessenger);
				BMessage notifyUI(MSG_AI_TITLE_CHANGED);
				notifyUI.AddString("title", title);
				TargetMessenger.SendMessage(&notifyUI);

				break;
			}
			default:
				BApplication::MessageReceived(msg);
				break;
		}
	}
};


int main(int argc, char** argv)
{
	// Usiamo la sincronizzazione immediata sui flussi di log per evitare buffering nel terminale
	setvbuf(stderr, NULL, _IONBF, 0);
	setvbuf(stdout, NULL, _IONBF, 0);

	fprintf(stderr, "ai_server: main() avviato.\n");
	AIServerApp app;
	app.Run();
	fprintf(stderr, "ai_server: Server spento.\n");
	return B_OK;
}
