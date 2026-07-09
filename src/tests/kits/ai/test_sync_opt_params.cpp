/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
// Test for context_id
#include <AICommands.h>
#include <String.h>
#include <stdio.h>

int main(int argc, char** argv) {
    AIEngine* ai = nullptr;
    BString question;

    if (argc == 2) {
    	question.Append(argv[1]); // spaces should be escaped
        ai = new AIEngine();
    } else if (argc == 3) {
		question.Append(argv[2]); // spaces should be escaped
        const char* context_id = argv[1];
        ai = new AIEngine(context_id); // Assegnato correttamente ad 'ai'
    } else if (argc >= 5) { // Corretto >5 in >=5 o secondo logica parametri
        const char* context_id = argv[1];
        const char* apiKey = argv[2];
        const char* modelName = argv[3];
        const char* pluginName = argv[4];
        
        for (int i = 5; i < argc; i++) { // spaces can be unescaped!!
            question.Append(argv[i]);
            if (i < argc - 1) question.Append(" ");
        }
        // Nota: Assicurati che nel costruttore di AIEngine ci sia questa firma a 5 parametri
        ai = new AIEngine(context_id, pluginName, modelName, apiKey); 
    }

    if (ai == nullptr) {
        printf("Parametri non validi o costruttore non inizializzato.\n");
        return 1;
    }
	
    BString status;
    BString response;

    // 1. Controllo dello stato dell'ai_server
    if (ai->GetStatus(status) == B_OK) {
        printf("Server status: %s\n", status.String());
    } else {
        printf("ai_server non risponde o non è avviato!\n");
        return 1;
    }

    // 2. Generazione testo sincrona con il prompt
    printf("\nInvio del prompt esplicito...\n");
    status_t s = ai->Generate(question.String(), response);
    
    if (s == B_OK) {
        printf("\n--- Risposta Pulita ---\n");
        printf("%s\n", response.String());
        printf("------------------------------\n");
    } else {
        printf("\nGenerazione fallita (Codice errore: %d).\n", (int)s);
        printf("Dettagli/Payload: %s\n", response.String());
        return 1;
    }
    
    BString finalCtxID;
    BString chatTitle;
    
    if (ai->GetContextID(finalCtxID) == B_OK) {
        printf("\n[INFO] ID Contesto utilizzato/creato dal server: %s\n", finalCtxID.String());
    }
    if (ai->GetTitle(chatTitle) == B_OK) {
        printf("[INFO] Titolo assegnato alla sessione: \"%s\"\n", chatTitle.String());
    }

    return 0;
}
