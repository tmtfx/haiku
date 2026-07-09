/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include <AICommands.h>
#include <String.h>
#include <stdio.h>

int main(int argc, char** argv) {
    (void)argc; (void)argv;

    AIEngine ai; // Inizializza automaticamente usando le impostazioni della Preflet
    BString status;
    BString response;

    // 1. Controllo dello stato del server (Ping)
    if (ai.GetStatus(status) == B_OK) {
        printf("server status: %s\n", status.String());
    } else {
        printf("ai_server not running or failed to respond\n");
        return 1;
    }

    // 2. Generazione testo sincrona (Prompt)
    printf("Sending prompt...\n");
    status_t s = ai.Generate("What is the capital of Friuli?", response);//Hello from Test via AI Kit API!
    
    if (s == B_OK) {
        printf("response:\n%s\n", response.String());
    } else {
        printf("Generation failed (Error code: %d). Details/Error: %s\n", s, response.String());
        return 1;
    }

    return 0;
}
