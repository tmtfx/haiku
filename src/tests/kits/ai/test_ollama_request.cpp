/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include <AICommands.h>
#include <String.h>
#include <stdio.h>

int main(int argc, char** argv) {
    (void)argc; (void)argv;

    // Inizializza l'istanza basandosi sul plugin attivo nella Preflet (es. Ollama)
    AIEngine ai; 
    BString response;

    printf("Inviando prompt a Ollama tramite ai_server...\n");
    status_t s = ai.Generate("Test prompt from TestOllama via AI Kit API", response);
    
    if (s == B_OK) {
        printf("Risposta da Ollama:\n%s\n", response.String());
    } else {
        printf("Errore di generazione: %s\n", response.String());
        return 1;
    }

    return 0;
}
