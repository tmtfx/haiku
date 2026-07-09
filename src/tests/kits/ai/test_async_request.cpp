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
        // 1. Istanziamo AIEngine (la classe definita in AICommands.h)
        if (fArgc == 1) {
        	printf("[CLIENT] Inizializzazione AIEngine (Impostazioni globali)...\n");
            fEngine = new AIEngine();
        } else if (fArgc == 2) {
            printf("[CLIENT] Inizializzazione AIEngine da contesto precedente...\n");
            const char* context_id = fArgv[1];
            fEngine = new AIEngine(context_id);
        } else if (fArgc == 4) {
            printf("[CLIENT] Inizializzazione AIEngine Custom...\n");
            const char* apiKey = fArgv[1];
            const char* model = (BString(fArgv[2]).IsEmpty() == false) ? fArgv[2] : "gemini-2.5-flash";
            const char* engine = (BString(fArgv[3]).IsEmpty() == false) ? fArgv[3] : "GeminiPlugin";
            fEngine = new AIEngine(engine, model, apiKey);
        }

        // 2. Controllo dello stato del server
        BString status;
        if (fEngine->GetStatus(status) != B_OK || status != "ok") {
            printf("[CLIENT ERRORE] l'ai_server non risponde o non è attivo!\n");
            PostMessage(B_QUIT_REQUESTED);
            return;
        }
        printf("[CLIENT] Stato ai_server: %s\n", status.String());

        // 3. Prepariamo il target a cui inviare i token parziali
        BMessenger myMessenger(this);

        const char* prompt = "Spiega l'architettura a messaggi di Haiku OS in una riga poetica.";
        printf("\n[CLIENT] Lancio generazione asincrona...\n");
        printf("[CLIENT] Prompt: \"%s\"\n\n", prompt);
        printf("--- Flusso di Risposta ---\n");

        // 4. Chiamata asincrona ufficiale del vostro Kit
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
                    printf("[Partial text] %s\n", partialText);
                    fflush(stdout); 
                }
                
                if (fullResponse && !partialText) {
                    printf("[Full response] %s\n", fullResponse);
                    fflush(stdout);
                }

                if (complete) {
                    printf("\n--------------------------------------------\n");
                    printf("[CLIENT] Ricezione completata.\n");
                    PostMessage(B_QUIT_REQUESTED); 
                }
                break;
            }

            case MSG_AI_ERROR: {
                const char* err = nullptr;
                msg->FindString("error", &err);
                printf("\n[CLIENT ERRORE SERVER] %s\n", err ? err : "Errore generico.");
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

