/*
 * VolumeIcons.h - risoluzione delle icone di volumi e partizioni (Haiku).
 *
 * Licenza: MIT. Prendi, modifica, incolla nel tuo progetto.
 *
 * Idea di fondo: chiedere l'icona *al dispositivo* (HVIF) e rasterizzarla
 * alla dimensione esatta della riga, invece di sperare in una bitmap 32x32
 * e nel ramo "segnaposto".
 *
 * Ordine di risoluzione (lo stesso che usa Tracker):
 *   volume montato : BEOS:ICON sul root del volume   -> icona del device
 *   non montato    : icona del device (via driver)   -> icona MIME generica
 *
 * Tutte le funzioni restituiscono una BBitmap nuova, da cancellare a carico
 * del chiamante (il tuo UnifiedListItem la cancella gia' nel distruttore),
 * oppure NULL se non e' stato possibile ricavare un'icona.
 */

#ifndef VOLUME_ICONS_H
#define VOLUME_ICONS_H

#include <SupportDefs.h>

class BBitmap;
class BNode;
class BPartition;
class BVolume;

namespace volumeicons {

// Rasterizza dati HVIF (B_VECTOR_ICON_TYPE) in una bitmap RGBA32 px x px.
BBitmap* RenderVector(const uint8* data, size_t size, float px);

// Icona impostata dall'utente sul nodo (attributo BEOS:ICON, HVIF o raster).
BBitmap* FromNode(BNode& node, float px);

// Icona "di classe" del disco, chiesta al driver tramite il device path
// (/dev/disk/...). Funziona anche a partizione non montata.
BBitmap* FromDevicePath(const char* devicePath, float px);

// Icona MIME generica dei volumi ("application/x-vnd.Be-volume"): quella
// che mostra Tracker quando il device non fornisce nulla.
BBitmap* GenericVolume(float px);

// Punto d'ingresso consigliato: gestisce sia la partizione montata sia
// quella smontata. Non restituisce mai l'icona "finta" del tuo .rdef:
// se torna NULL tocca a te decidere il ripiego.
BBitmap* ForPartition(const BPartition* partition, float px);

// Come sopra, ma partendo da un BVolume (es. volumi di rete condivisi).
BBitmap* ForVolume(const BVolume& volume, float px);

}	// namespace volumeicons

#endif	// VOLUME_ICONS_H
