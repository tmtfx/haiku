/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef _X86_CPU_RNG_H
#define _X86_CPU_RNG_H

#include <SupportDefs.h>


/* * Inizializza e registra l'algoritmo RDRAND/RDSEED nel framework BCrypto.
 * Rileva automaticamente se la CPU supporta le istruzioni necessarie.
 */
status_t BInitx86CPURNG();


#endif // _X86_CPU_RNG_H
