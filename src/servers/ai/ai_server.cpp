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

#include <os/ai/AIPlugin.h>
#include <AIConfig.h>
#include <AICommands.h>
#include "ai_server.h"
#include "mcp_manager.h"


const char* kServerSignature = "application/x-vnd.Haiku-ai_server";

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
	fprintf(stderr, "ai_server: [LOADER] Scansione directory: %s\n", dirpath);
	BDirectory dir(dirpath);
	if (dir.InitCheck() != B_OK) {
		fprintf(stderr, "ai_server: [LOADER] Impossibile aprire directory: %s\n", dirpath);
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
				fprintf(stderr, "ai_server: [LOADER] File '%s' ha attributo AI:plugin_type = '%s'\n", name, pType.String());
			} else {
				fprintf(stderr, "ai_server: [LOADER] File '%s' ATTENZIONE: nessun attributo AI:plugin_type trovato. Uso fallback: '%s'\n", name, pType.String());
			}
		} else {
			fprintf(stderr, "ai_server: [LOADER] Errore BNode InitCheck per file '%s'\n", name);
		}
		// -----------------------------

		void* h = dlopen(path.Path(), RTLD_NOW);
		if (!h) {
			fprintf(stderr, "ai_server: [LOADER] dlopen fallito per '%s': %s\n", name, dlerror());
			continue;
		}

		auto init = (ai_plugin_t (*)(void)) dlsym(h, "ai_plugin_init");
		auto fin = (void (*)(ai_plugin_t)) dlsym(h, "ai_plugin_free");
		if (!init || !fin) {
			fprintf(stderr, "ai_server: [LOADER] '%s' manca di simboli obbligatori (init/free)\n", name);
			dlclose(h);
			continue;
		}

		auto gen_sync = (status_t (*)(ai_plugin_t, const char*, char*, size_t, BMessage*)) dlsym(h, "ai_plugin_generate_text_sync");
		auto gen_async = (status_t (*)(ai_plugin_t, const char*, BMessage*)) dlsym(h, "ai_plugin_generate_text_async");
		auto get_cap = (uint32 (*)(void)) dlsym(h, "ai_plugin_get_capabilities");
		auto list_models = (status_t (*)(const BMessage*, char*, size_t)) dlsym(h, "ai_plugin_list_models");
		auto set_model = (status_t (*)(ai_plugin_t, const char*)) dlsym(h, "ai_plugin_set_model");

		const char* (*get_name)(void) = (const char* (*)(void))dlsym(h, "get_plugin_name");

		AISettings s;
		LoadAISettings(s);

		// 2. Costruiamo il BMessage completo da passare al plugin nativo
		BMessage configMsg('AISC');
		configMsg.AddString("engine", s.engine.String());
		configMsg.AddString("plugin", s.plugin.String());
		configMsg.AddString("model_name", s.model.String()); // Usiamo "model_name" coerente con il contesto
		configMsg.AddString("api_key", s.api_key.String());

		// 3. Inizializziamo l'istanza passando il puntatore al BMessage
		ai_plugin_t inst = init();
		if (!inst) {
			fprintf(stderr, "ai_server: [LOADER] Inizializzazione istanza fallita per '%s'\n", name);
			dlclose(h);
			continue;
		}

		uint32 caps = 0;
		if (get_cap != nullptr) {
			caps = get_cap();
		}

		// Il plugin è valido se espone ALMENO una capacità nel bitmask, OPPURE se permette il listing dei modelli
		if (caps == 0 && !list_models) {
			fprintf(stderr, "ai_server: [LOADER] '%s' non dichiara capacità e non elenca modelli, scartato\n", name);
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
		e.set_model = set_model;

		if (get_name != nullptr) {
			e.name = get_name();
		} else {
			e.name = path.Leaf();
		}
		

		
		if (s.plugin == e.name) {
			if (e.set_model && s.model.Length() > 0) {
				e.set_model(e.instance, s.model.String());
				fprintf(stderr, "ai_server: [LOADER] Modello predefinito '%s' applicato a %s\n", s.model.String(), e.name.String());
			}
		}
		
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
	context->RemoveName("messages");
	context->AddMessage("messages", &historyMsg);
	
	return B_OK;
}


// Salva (o aggiorna) il contesto nativo su disco
status_t save_chat_context(const char* contextID, BMessage* context) {
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
	outContext->AddString("title", "Nuova Conversazione");
	outContext->AddString("plugin_name", defaultPlugin);
	outContext->AddString("model_name", defaultModel);
	outContext->AddString("remote_id", ""); // Vuoto di default (modalità locale)
	
	// Creiamo un messaggio contenitore vuoto per la cronologia
	BMessage emptyHistory;
	outContext->AddMessage("messages", &emptyHistory);
	
	return save_chat_context(contextID, outContext);
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
				break;
			}
			
			case MSG_GET_SESSION_INFO: {
				int32 sessionID = -1;
				BString contextID = "ctx_default"; // Fallback speculare a MSG_GEN_SYNC

				msg->FindInt32("session_id", &sessionID);
				
				// 1. Determiniamo il contextID associato alla sessione (se esiste) o alla richiesta
				const char* reqContextID = msg->FindString("context_id");
				if (reqContextID && reqContextID[0] != '\0') {
					contextID = reqContextID;
				} else if (sessionID != -1 && gSessions.count(sessionID) > 0) {
					ClientSession& session = gSessions[sessionID];
					if (session.context_id.Length() > 0) {
						contextID = session.context_id;
					}
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

					chatContext.FindString("title", &title);
					chatContext.FindString("plugin_name", &pluginName);
					chatContext.FindString("model_name", &modelName);
					chatContext.FindString("remote_id", &remoteId);

					// 3. Se il titolo è rimasto quello di default, proviamo a generarne uno euristico
					if (title == "Nuova Conversazione") {
						BMessage historyMsg;
						if (chatContext.FindMessage("messages", &historyMsg) == B_OK) {
							BMessage firstMsg;
							// Vediamo se c'è almeno un messaggio per estrarre il titolo
							if (historyMsg.FindMessage("msg", 0, &firstMsg) == B_OK) {
								BString content;
								if (firstMsg.FindString("content", &content) == B_OK && !content.IsEmpty()) {
									title = content;
									if (title.Length() > 30) {
										title.Truncate(30);
										title.Append("...");
									}
									// Aggiorniamo il file su disco così la generazione è persistente
									chatContext.RemoveName("title");
									chatContext.AddString("title", title.String());
									save_chat_context(contextID.String(), &chatContext);
								}
							}
						}
					}

					// Impacchettiamo i dati reali del file per il client
					reply.AddInt32("status", B_OK);
					reply.AddString("context_id", contextID.String());
					reply.AddString("title", title.String());
					reply.AddString("plugin_name", pluginName.String());
					reply.AddString("model_name", modelName.String());
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
				
				// Aggiungiamo il conteggio totale delle sessioni attive
				reply.AddInt32("count", (int32)gSessions.size());

				for (const auto& pair : gSessions) {
					const ClientSession& session = pair.second;
					BMessage sessionInfo;

					sessionInfo.AddInt32("session_id", session.id);
					sessionInfo.AddString("context_id", session.context_id.String());
					sessionInfo.AddString("plugin_name", session.plugin_name.String());
					sessionInfo.AddString("model_name", session.model_name.String());

					// Recuperiamo il titolo dal file di contesto usando la logica che hai già
					BMessage chatContext;
					BString title = "Nuova Conversazione";
					BString remoteId;
					
					if (load_or_create_chat_context(session.context_id.String(), &chatContext) == B_OK) {
						chatContext.FindString("title", &title);
						chatContext.FindString("remote_id", &remoteId);
					}
					sessionInfo.AddString("title", title.String());
					if (!remoteId.IsEmpty()) {
						sessionInfo.AddString("remote_id", remoteId.String());
					}

					// Impacchettiamo la sessione dentro il messaggio di risposta globale
					reply.AddMessage("session", &sessionInfo);
				}

				msg->SendReply(&reply);
				break;
			}
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

				PluginEntry* p = nullptr;
				BString pluginName;
				BString modelName;
				BString apiKey;
				BString contextID = "ctx_default";

				// 1. Risoluzione dei parametri
				const char* customPlugin = msg->FindString("custom_plugin");
				const char* customModel = msg->FindString("custom_model");
				const char* customApiKey = msg->FindString("custom_api_key");
				const char* reqContextID = msg->FindString("context_id");

				if (reqContextID && reqContextID[0] != '\0') {
					contextID = reqContextID;
				}

				if (customPlugin && customModel) {
					pluginName = customPlugin;
					modelName = customModel;
					apiKey = customApiKey ? customApiKey : "";

					for (auto& pe : gPlugins) {
						if (pe.name == customPlugin || pe.name.FindFirst(customPlugin) >= 0) {
							p = &pe;
							break;
						}
					}
				} else if (gSessions.count(sessionID) > 0) {
					ClientSession& session = gSessions[sessionID];
					pluginName = session.plugin_name;
					modelName = session.model_name;
					apiKey = session.custom_api_key;
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
				if (modelName.IsEmpty()) modelName = "gemini-2.5-flash";

				// 2. Carichiamo on-demand il contesto BMessage da disco
				BMessage* chatContext = new BMessage();
				load_or_create_chat_context(contextID.String(), chatContext, pluginName.String(), modelName.String());

				// Recupero della chiave se vuota
				if (apiKey.IsEmpty()) {
					GetPluginAPIKey(p->name.String(), apiKey);
				}

				// 3. Arricchiamo il BMessage con i dati della sessione
				chatContext->RemoveName("api_key");
				chatContext->AddString("api_key", apiKey.String());
				chatContext->RemoveName("model_name");
				chatContext->AddString("model_name", modelName.String());

				// 4. Propaghiamo use_remote_context
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

				// CORREZIONE 1: Sincronizzazione forzata stato locale
				if (!useRemoteCtxAsync) {
					chatContext->RemoveName("remote_id");
					chatContext->AddString("remote_id", "");
					
					// 5. In modalità locale salviamo subito il prompt dell'utente
					append_message_to_context(chatContext, "user", prompt);
					save_chat_context(contextID.String(), chatContext);
				}

				// 6. Percorso temporaneo per lo stream
				char tmpPath[PATH_MAX];
				snprintf(tmpPath, sizeof(tmpPath), "/tmp/ai_stream_%d_%lu.tmp", (int)getpid(), (unsigned long)rand());

				chatContext->RemoveName("notify_path");
				chatContext->AddString("notify_path", tmpPath);

				// Risposta immediata di ACK per sbloccare il client BLooper
				BMessage ack;
				ack.AddString("status", "ok");
				msg->SendReply(&ack);

				// === INTEGRAZIONE MCP: Preparazione del contesto asincrono ===
				//AsyncToolContext* mcpCtx = new AsyncToolContext();
				//mcpCtx->clientTarget = target;
				//mcpCtx->sessionID = sessionID;
				//mcpCtx->contextID = contextID; // Nota: Se AsyncToolContext richiede std::string o const char*, usa contextID.String()

				// Invocazione asincrona del plugin
				ai_plugin_t instance = p->instance;
				std::string promptCopy = prompt;
				
				//chatContext->AddPointer("mcp_context", mcpCtx);
				chatContext->RemoveName("server_messenger");
				chatContext->AddMessenger("server_messenger", be_app_messenger);
				
				// Passiamo mcpCtx come quarto argomento alla funzione
				int rc = p->generate_async(instance, promptCopy.c_str(), chatContext);
				if (rc != 0) {
					BMessage err(MSG_AI_ERROR);
					err.AddString("error", "plugin async failed to start");
					target.SendMessage(&err);
					
					//delete mcpCtx;			 // Pulizia del contesto MCP in caso di fallimento immediato
					delete chatContext;
					break;
				}

				int32 tokenLimit = 4000; 
				msg->FindInt32("token_limit", &tokenLimit);

				// 7. Lancio del thread Watcher locale modificato (Cattura mcpCtx nell'ambiente della lambda)
				std::thread watcher([target, tmpPath = std::string(tmpPath), contextID, chatContext, tokenLimit, useRemoteCtxAsync]() mutable {
					FILE* f = NULL;
					size_t lastSize = 0;
					bool done = false;
					BString fullResponseAccumulator("");

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
								
								BString s(buf.data());
								int32 pos = s.FindFirst("<<STREAM_END>>");
								if (pos != B_ERROR) {
									BString part;
									s.CopyInto(part, 0, pos);
									if (part.Length() > 0) {
										BMessage out(MSG_AI_RESPONSE);
										out.AddString("partial", part.String());
										out.AddBool("complete", false);
										target.SendMessage(&out);
										fullResponseAccumulator << part;
									}
									
									// Stream completato con successo
									BMessage fin(MSG_AI_RESPONSE);
									fin.AddString("response", fullResponseAccumulator.String());
									fin.AddBool("complete", true);
									fin.AddInt32("status", 0);
									target.SendMessage(&fin);
									
									// Pulizia metadati volatili prima del dump finale
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
										append_message_to_context(chatContext, "assistant", fullResponseAccumulator.String());
										save_chat_context(contextID.String(), chatContext);
									}
									
									done = true;
									break;
								} else {
									BMessage out(MSG_AI_RESPONSE);
									out.AddString("partial", s.String());
									out.AddBool("complete", false);
									target.SendMessage(&out);
									fullResponseAccumulator << s;
								}
							}
						}
						snooze(50000); // 50ms
					}
					if (f) fclose(f);
					remove(tmpPath.c_str());
					
					// == PULIZIA FINALE ASINCRONA ==
					delete chatContext; // Liberiamo la memoria del contesto
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
					if (session.context_id.Length() > 0) {
						contextID = session.context_id;
					}

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

				// 2. Recupero API Key
				if (apiKey.IsEmpty()) {
					GetPluginAPIKey(p->name.String(), apiKey);
				}

				// 3. Prepariamo i metadati volatili per il plugin
				chatContext.RemoveName("api_key");
				chatContext.AddString("api_key", apiKey.String());
				chatContext.RemoveName("model_name");
				chatContext.AddString("model_name", modelName.String());

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

				// CORREZIONE 1: Se la sessione è forzata in LOCALE, puliamo l'eventuale remote_id residuo
				// nel messaggio per evitare che il plugin si confonda.
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

				BMessage r('RESP');
				if (rc == 0) {
					r.AddString("response", response);
					r.AddInt32("status", 0);

					// Pulizia dati volatili prima del salvataggio definitivo su disco
					chatContext.RemoveName("api_key");
					chatContext.RemoveName("use_remote_context");

					if (useRemoteCtx) {
						// 6a. Contesto remoto: Il plugin ha generato/aggiornato il remote_id in chatContext
						const char* updatedRemoteId = nullptr;
						if (chatContext.FindString("remote_id", &updatedRemoteId) == B_OK
							&& updatedRemoteId && updatedRemoteId[0] != '\0') {
							
							// CORREZIONE 2: Usiamo direttamente chatContext senza ricaricarlo.
							// In questo modo preserviamo i vecchi messaggi locali e aggiorniamo solo l'ID.
							save_chat_context(contextID.String(), &chatContext);
						}
					} else {
						// 6b. Contesto locale: salviamo la risposta dell'assistente nello storico
						append_message_to_context(&chatContext, "assistant", response);
						save_chat_context(contextID.String(), &chatContext);
					}
				} else {
					r.AddString("error", "plugin generation failed");
					r.AddInt32("status", rc);
				}

				msg->SendReply(&r);
				break;
			}
			case MSG_ABORT_SESSION:
			{
			    BString contextId = msg->FindString("context_id");
			    if (!contextId.IsEmpty()) {
			        // Cerchiamo la sessione attiva
			        for (auto& pair : gSessions) { 
			            if (pair.second.context_id == contextId) {
			                pair.second.abort_requested = true; // Attiviamo il Kill Switch!
			                fprintf(stderr, "[AI_SERVER] [KILL SWITCH] Richiesta di interruzione ricevuta per la sessione: %s\n", contextId.String());

			                BMessage reply(B_REPLY);
			                reply.AddInt32("status", B_OK);
			                msg->SendReply(&reply);
			                break;
			            }
			        }
			    }
			    break;
			}
			case MSG_CHECK_ABORT:
			{
			    BString contextId = msg->FindString("context_id");
			    BMessage reply(B_REPLY);
			    reply.AddInt32("status", B_OK); // Default: non interrotto
    
			    if (!contextId.IsEmpty()) {
			        for (auto& pair : gSessions) { 
			            if (pair.second.context_id == contextId) {
			                if (pair.second.abort_requested) {
			                    reply.AddInt32("status", B_CANCELED); // Segnala l'interruzione!
			                }
			                break;
			            }
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
                    session->abort_requested = false; // Resettiamo il flag per le prossime chat

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
                        if (ExtractStringFromJson(argsJson.String(), "pattern", pattern, false)) {
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
                }  
                bool isCritical = false;
                BString details = "";

                if (toolName == "run_terminal_command") {
                    isCritical = true;
                    const char* cmdToRun = arguments.FindString("cmd");
                    details.SetToFormat("Vuole eseguire il comando terminale:\n\n  \"%s\"", 
                                        cmdToRun ? cmdToRun : "N/A");
                } 
                else if (toolName == "create_file") {
                    isCritical = true;
                    const char* filePath = arguments.FindString("path");
                    details.SetToFormat("Vuole creare o sovrascrivere il file:\n\n  \"%s\"", 
                                        filePath ? filePath : "N/A");
                } 
                else if (toolName == "make_directory") {
                    isCritical = true;
                    const char* filePath = arguments.FindString("path");
                    details.SetToFormat("Vuole creare la cartella:\n\n  \"%s\"", 
                                        filePath ? filePath : "N/A");
                } 
                else if (toolName == "delete_file") {
                    isCritical = true;
                    const char* filePath = arguments.FindString("path");
                    details.SetToFormat("Vuole ELIMINARE definitivamente:\n\n  \"%s\"", 
                                        filePath ? filePath : "N/A");
                } 
                else if (toolName == "open_document") {
                    isCritical = true;
                    const char* filePath = arguments.FindString("path");
                    details.SetToFormat("Vuole aprire il documento o avviare l'applicazione:\n\n  \"%s\"", 
                                        filePath ? filePath : "N/A");
                } 
                else if (toolName == "show_alert_dialog") {
                    isCritical = true;
                    const char* text = arguments.FindString("text");
                    details.SetToFormat("Vuole mostrare un messaggio di avviso a schermo:\n\n  \"%s\"", 
                                        text ? text : "N/A");
                } 
                else if (toolName == "manage_attribute") {
                    // Questa è un'operazione BFS speciale: controlliamo l'azione richiesta!
                    const char* action = arguments.FindString("action");
                    if (action && strcmp(action, "write") == 0) {
                        isCritical = true;
                        const char* filePath = arguments.FindString("path");
                        const char* attrName = arguments.FindString("name");
                        const char* attrValue = arguments.FindString("value");
                        details.SetToFormat(
                            "Vuole scrivere l'attributo BFS '%s' con valore '%s'\n"
                            "sul file:\n\n  \"%s\"", 
                            attrName ? attrName : "N/A", 
                            attrValue ? attrValue : "N/A", 
                            filePath ? filePath : "N/A"
                        );
                    }
                }
                
                if (isCritical) {
                    BString alertText;
                    alertText.SetToFormat(
                        "L'assistente AI richiede l'autorizzazione per eseguire un'operazione critica.\n\n"
                        "Strumento richiesto: %s\n"
                        "%s\n\n"
                        "Vuoi consentire questa operazione?",
                        toolName.String(),
                        details.String()
                    );

                    // Mostriamo il BAlert nativo di Haiku
                    BAlert* safetyAlert = new BAlert(
                        "Sicurezza AI (MCP)",
                        alertText.String(),
                        "Rifiuta",      // Pulsante 0 (Ritorna 0, mappato su ESC/Default)
                        "Consenti",     // Pulsante 1 (Ritorna 1)
                        "Interrompi Loop AI",
                        B_WIDTH_AS_USUAL,
                        B_WARNING_ALERT // Icona di pericolo gialla
                    );
                    
                    safetyAlert->SetShortcut(0, B_ESCAPE); // Esc per rifiutare al volo
                    
                    int32 choice = safetyAlert->Go();
                    if (choice == 2) {
                        // L'utente vuole fermare l'intera catena di ragionamento dell'AI!
                        fprintf(stderr, "[AI_SERVER] [SICUREZZA] INTERRUZIONE FORZATA richiesta dall'utente.\n");
                        
                        // 1. Impostiamo un flag di interruzione sulla sessione in modo che il thread del plugin 
                        // sappia che non deve più elaborare ulteriori risposte o loop.
                        session->abort_requested = true; 

                        // 2. Rispondiamo con un errore fatale che costringe il client a chiudere la chiamata
                        reply.AddInt32("status", B_CANCELED);
                        reply.AddString("result", "{\"error\":\"Aborted: L'utente ha interrotto il loop di elaborazione dell'AI.\"}");
                        msg->SendReply(&reply);
                        break;
                    }
                    
                    if (choice == 0) {
                        // L'utente ha negato l'autorizzazione
                        fprintf(stderr, "[AI_SERVER] [SICUREZZA] Accesso negato dall'utente per '%s'\n", toolName.String());
                        
                        reply.AddInt32("status", B_PERMISSION_DENIED);
                        reply.AddString("result", "{\"error\":\"Errore: L'utente di Haiku ha negato l'autorizzazione per eseguire questa azione di scrittura/modifica.\"}");
                        msg->SendReply(&reply);
                        break;
                    }
                    
                    fprintf(stderr, "[AI_SERVER] [SICUREZZA] Accesso consentito dall'utente per '%s'\n", toolName.String());
                }


                // 4. DELEGA ALL'ESECUTORE CENTRALE (mcp_manager.cpp)
                fprintf(stderr, "[AI_SERVER] Sicurezza superata. Delegando esecuzione a ExecuteLocalTool per: '%s'\n", toolName.String());
                
                resultOutput = ExecuteLocalTool(toolName.String(), arguments);
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
						pluginList.AddString("plugin_name", pth.Leaf());
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
				char buffer[4096] = "[]"; 

				// 1. Recuperiamo la chiave reale per questo plugin dal KeyStore
				BString apiKey;
				GetPluginAPIKey(pluginName.String(), apiKey);

				// TODO: Se hai anche un base_url salvato nelle impostazioni del server, recuperalo qui.
				BString baseUrl = ""; 

				// 2. Componiamo il BMessage nativo di configurazione al posto del vecchio JSON string
				BMessage configMsg('AISC');
				configMsg.AddString("api_key", apiKey.String());
				configMsg.AddString("base_url", baseUrl.String());
				configMsg.AddString("plugin", pluginName.String());

				// 3. Cerchiamo il plugin specifico in memoria
				for (const auto& p : gPlugins) {
					// Controllo flessibile sia sul nome foglia del path che sul nome registrato del plugin
					BPath pth(p.path.c_str());
					if (pluginName == pth.Leaf() || pluginName == p.name) {
						if (p.list_models) {
							fprintf(stderr, "[DEBUG SERVER] Richiesta modelli per '%s' tramite BMessage nativo...\n", p.name.String());
							
							// 4. Passiamo il BMessage di configurazione e il buffer di destinazione
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

				// 1. Carichiamo le impostazioni globali aggiornate dal server
				if (LoadAISettings(s)) {
					BString apiKey;
					GetPluginAPIKey(s.plugin.String(), apiKey);

					// 2. Controlliamo le sessioni attive per verificare quali usano il plugin da applicare
					// e se hanno useCustomAPIKey impostato a false.
					for (auto& pair : gSessions) {
						ClientSession& session = pair.second;
						if (session.plugin_name == s.plugin && !session.useCustomAPIKey) {
							// Recuperiamo il titolo della chat per mostrarlo nel BAlert
							BString chatTitle = "Nuova Conversazione";
							BMessage chatContext;
							if (load_or_create_chat_context(session.context_id.String(), &chatContext) == B_OK) {
								chatContext.FindString("title", &chatTitle);
							}

							BString alertText;
							alertText.SetToFormat(
								"La sessione attiva \"%s\" sta usando il plugin \"%s\".\n\n"
								"La configurazione globale è stata aggiornata.\n"
								"Vuoi applicare il nuovo modello \"%s\" e la nuova configurazione a questa sessione?",
								chatTitle.String(),
								s.plugin.String(),
								s.model.String()
							);

							BAlert* alert = new BAlert(
								"Aggiorna Sessione AI",
								alertText.String(),
								"No",
								"Sì",
								nullptr,
								B_WIDTH_AS_USUAL,
								B_INFO_ALERT
							);

							if (alert->Go() == 1) {
								session.model_name = s.model;
								session.custom_api_key = apiKey;

								// Aggiorniamo anche il file di contesto salvato su disco
								if (load_or_create_chat_context(session.context_id.String(), &chatContext) == B_OK) {
									chatContext.RemoveName("model_name");
									chatContext.AddString("model_name", s.model.String());
									save_chat_context(session.context_id.String(), &chatContext);
								}
								applied++;
							}
						}
					}

					// Notifichiamo il plugin attivo del cambio di modello al volo
					for (auto &pe : gPlugins) {
						if (s.plugin == pe.name) {
							if (pe.set_model && s.model.Length() > 0) {
								pe.set_model(pe.instance, s.model.String());
								applied++;
							}
							break; // Trovato il plugin globale attivo, possiamo uscire dal ciclo
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
	fprintf(stderr, "ai_server: Chiamata a app.Run()...\n");
	app.Run();
	fprintf(stderr, "ai_server: Server spento.\n");
	return B_OK;
}
