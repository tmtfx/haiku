/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef _B_CRYPTO_DEVICE_H_
#define _B_CRYPTO_DEVICE_H_

#include "BCryptoDefs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Inizializza le strutture dati del manager dei dispositivi (array/liste e mutex).
 * Chiamato da BCryptoCore durante l'init del driver.
 */
status_t crypto_manager_init(void);

/**
 * Distrugge i mutex e pulisce le risorse del manager.
 */
void crypto_manager_uninit(void);

/**
 * Registra un nuovo fornitore hardware (es. CPU, scheda PCIe).
 * @param info Puntatore alla struttura info (viene copiata internamente).
 */
status_t register_crypto_device(crypto_device_info* info);

/**
 * Cerca tra i dispositivi registrati quello che supporta l'algoritmo
 * richiesto con il throughput (prestazioni) maggiore.
 * * @param algo L'ID dell'algoritmo cercato (es. B_CRYPTO_AES).
 * @return Un puntatore alla struttura info del miglior dispositivo, o NULL se nessuno trovato.
 */
crypto_device_info* find_best_device(BCryptoAlgorithmID algo);

/**
 * Restituisce il numero di dispositivi hardware registrati.
 */
int32 get_registered_device_count(void);

#ifdef __cplusplus
}
#endif

#endif // _B_CRYPTO_DEVICE_H_
