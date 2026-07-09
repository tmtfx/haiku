/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include <crypto/BCryptoDefs.h>

int main() {
    // Apriamo il dispositivo API del framework
    int fd = open("/dev/crypto/v1", O_RDWR);
    if (fd < 0) {
        perror("Errore: Impossibile aprire /dev/crypto/v1");
        return 1;
    }

    printf("BCrypto Framework - Algoritmi Registrati nel Core\n");
    printf("------------------------------------------------------------------\n");
    printf("%-8s %-12s %-30s\n", "ID", "Tipo", "Descrizione (Modulo)");
    printf("------------------------------------------------------------------\n");

    BCryptoAlgorithmInfo info;
    memset(&info, 0, sizeof(info));
    info.cookie = 0; // Il driver inizierà dal primo elemento della lista sAlgorithms

    // Continuiamo a chiamare l'IOCTL finché il Core ha algoritmi da elencarci
    while (ioctl(fd, B_CRYPTO_IOCTL_GET_NEXT_ALGO, &info) == B_OK) {
        const char* typeStr = (info.flags & B_CRYPTO_ALG_HW_ACCEL) ? "Hardware" : "Software";
        
        // info.vendor ora contiene la stringa composta durante l'init del modulo
        // (es. "AES-CBC (AESNI)" oppure "SHA256 (Software)")
        printf("0x%04x   %-12s %-30s\n", 
                info.id, typeStr, info.vendor);
        
        // Non serve incrementare info.cookie manualmente, lo fa il kernel per noi.
    }

    printf("------------------------------------------------------------------\n");

    close(fd);
    return 0;
}
