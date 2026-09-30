/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
/*#include <cstdio>
#include <cstddef>
#include <cstdarg>*/
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

void LogDebug(const char* format, ...) {
    // Nota: aprire/chiudere a ogni log è molto lento, ma garantisce 
    // che se il programma crasha subito dopo, il testo è sul file.
    FILE* f = fopen("/boot/home/radeon_hd_debug.log", "a");
    if (f == NULL) return;
    
    va_list args;
    va_start(args, format);
    vfprintf(f, format, args);
    va_end(args);
    
    fflush(f);               // Svuota il buffer della libreria C
    fsync(fileno(f));        // Forza la scrittura fisica su disco (sincrono al 100%)
    fclose(f);
}

#ifdef __cplusplus
}
#endif
