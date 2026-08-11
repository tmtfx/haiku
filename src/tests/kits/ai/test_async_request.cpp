/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include <Application.h>
#include <Message.h>
#include <Messenger.h>
#include <SupportDefs.h>
#include <String.h>
#include <stdio.h>
#include <stdlib.h>

#include <AICommands.h>
#include <AIConfig.h>


class AsyncTestApp : public BApplication {
public:
	AsyncTestApp(int argc, char** argv)
		: BApplication("application/x-vnd.AI-AsyncTest"),
		  fArgc(argc),
		  fArgv(argv),
		  fEngine(nullptr)
	{
	}

	virtual void ReadyToRun() override {
		// 1. Istanziamo AIEngine in base agli argomenti passati da riga di comando
		if (fArgc == 1) {
			printf("[CLIENT] Inizializzazione AIEngine (Impostazioni globali)...\n");
			fEngine = new AIEngine();
		} else if (fArgc == 2) {
			printf("[CLIENT] Inizializzazione AIEngine da contesto precedente...\n");
			const char* context_id = fArgv[1];
			fEngine = new AIEngine(context_id);
		} else if (fArgc >= 4) {
			const char* apiKey = fArgv[1];
			const char* model = (BString(fArgv[2]).IsEmpty() == false) ? fArgv[2] : "gemini-3.5-flash";
			const char* plugin = (BString(fArgv[3]).IsEmpty() == false) ? fArgv[3] : "GeminiPlugin";
			const char* baseUrl = (fArgc >= 5 && BString(fArgv[4]).IsEmpty() == false) ? fArgv[4] : nullptr;

			printf("[CLIENT] Inizializzazione AIEngine Custom...\n");
			printf("         Plugin: %s | Modello: %s | BaseURL: %s\n", 
				plugin, model, baseUrl ? baseUrl : "(default)");

			// Usiamo il nuovo costruttore: (pluginName, modelName, apiKey, baseUrl)
			fEngine = new AIEngine(plugin, model, apiKey, baseUrl);
		} else {
			printf("Uso: %s [context_id]\n", fArgv[0]);
			printf("Oppure: %s <api_key> <model> <plugin> [base_url]\n", fArgv[0]);
			PostMessage(B_QUIT_REQUESTED);
			return;
		}

		// 2. Controllo dello stato del server
		BString status;
		if (fEngine->GetStatus(status) != B_OK || status != "ok") {
			printf("[CLIENT ERRORE] l'ai_server non risponde o non è attivo!\n");
			PostMessage(B_QUIT_REQUESTED);
			return;
		}
		printf("[CLIENT] Stato ai_server: %s\n", status.String());

		// (Opzionale) Esempio di configurazione del System Prompt col nuovo Kit
		fEngine->SetSystemPrompt("Sei un assistente conciso ed esperto di Haiku OS.");

		// 3. Prepariamo il target a cui inviare i token parziali
		BMessenger myMessenger(this);

		const char* prompt = "Spiega l'architettura a messaggi di Haiku OS in una riga poetica.";
		printf("\n[CLIENT] Lancio generazione asincrona...\n");
		printf("[CLIENT] Prompt: \"%s\"\n\n", prompt);
		printf("--- Flusso di Risposta ---\n");

		// 4. Chiamata asincrona ufficiale del Kit
		status_t err = fEngine->GenerateAsync(prompt, myMessenger);
		if (err != B_OK) {
			printf("\n[CLIENT ERRORE] GenerateAsync fallito (Codice: %d)\n", (int)err);
			PostMessage(B_QUIT_REQUESTED);
		}
	}

	virtual void MessageReceived(BMessage* msg) override {
		switch (msg->what) {
			case MSG_AI_RESPONSE: {
				bool complete = false;
				msg->FindBool("complete", &complete);

				const char* partialText = msg->FindString("partial");
				const char* fullResponse = msg->FindString("response");
				const char* errorText = msg->FindString("error");

				if (errorText) {
					printf("\n[SERVER ERRORE] %s\n", errorText);
				}

				if (partialText) {
					printf("%s", partialText);
					fflush(stdout); 
				}

				if (fullResponse && !partialText) {
					printf("%s", fullResponse);
					fflush(stdout);
				}

				if (complete) {
					printf("\n--------------------------------------------\n");
					printf("[CLIENT] Ricezione completata.\n");

					// Stampa ID contesto assegnato/aggiornato
					BString ctxId;
					if (fEngine->GetContextID(ctxId) == B_OK) {
						printf("[CLIENT] Contesto corrente: %s\n", ctxId.String());
					}

					PostMessage(B_QUIT_REQUESTED); 
				}
				break;
			}

			case MSG_AI_ERROR: {
				const char* err = nullptr;
                if (msg->FindString("error", &err) != B_OK) {
                    msg->FindString("error_message", &err);
                }

                int32 httpCode = 0;
                msg->FindInt32("http_code", &httpCode);

                if (httpCode > 0) {
                    printf("\n[CLIENT ERRORE SERVER (HTTP %d)] %s\n", (int)httpCode, err ? err : "Nessun dettaglio");
                } else {
                    printf("\n[CLIENT ERRORE SERVER] %s\n", err ? err : "Errore generico.");
                }

                //msg->PrintToStream();

                PostMessage(B_QUIT_REQUESTED);
                break;
			}

			default:
				BApplication::MessageReceived(msg);
				break;
		}
	}

	virtual ~AsyncTestApp() {
		delete fEngine;
	}

private:
	int fArgc;
	char** fArgv;
	AIEngine* fEngine;
};

int main(int argc, char** argv) {
	AsyncTestApp app(argc, argv);
	app.Run();
	return 0;
}
