# Piano di implementazione: cursori animati hardware-only in Haiku

## Obiettivo

Implementare cursori animati **solo** sui driver con supporto hardware cursor, evitando fallback software animato e mantenendo comportamento robusto su hardware legacy.

---

## Stato attuale verificato

1. I cursori di sistema predefiniti sono inglobati in `app_server` (`CursorData.cpp` incluso da `CursorManager.cpp`).
2. I cursori personalizzati runtime esistono già, ma solo statici (`AS_CREATE_CURSOR`, `AS_CREATE_CURSOR_BITMAP`).
3. La pipeline hardware animata (hook accelerant, gating, fallback statico) è presente.
4. Manca un flusso completo e coerente per la **sorgente dei frame animati** sia lato sistema sia lato custom runtime.

---

## Decisioni architetturali finali

## 1) Archiviazione asset cursori di sistema

- Si mantiene il file base esistente, ad esempio:
  - `data/artwork/cursors/Pointer`
- Si aggiunge una cartella sibling:
  - `data/artwork/cursors/Pointer.frames/`

Dentro `Pointer.frames/` ci saranno:
- frame bitmap aggiuntivi (`frame_01`, `frame_02`, ...),
- un **file di testo manifesto** (non metadati filesystem).

## 2) Campi canonici del manifesto testo

Il manifesto deve includere:
- `frame_count`
- `start_delay_ms`
- `idle_delay_ms`
- `speed`
- `trigger` (`immediate` / `on_idle`)

Questi campi rappresentano la semantica ufficiale dell’animazione cursore.

## 3) Scope di delivery

- **Fase 1:** cursori animati di sistema (predefiniti).
- **Fase 2:** cursori animati personalizzati runtime (app).

## 4) API custom runtime

- Per la fase 2 si prevede estensione **API pubblica `BCursor`** (non solo protocollo privato).
- Va mantenuta retrocompatibilità completa con i percorsi statici esistenti.

---

## Policy hardware/fallback

- `B_SUPPORTS_CURSOR_ANI` resta hook necessario per abilitare la feature.
- `B_CURSOR_SUPPORTED_FRAMES` limita i frame extra disponibili in hardware.
- Budget frame: massimo totale 16 (frame base + 15 extra).
- Se vincoli/capacità non sono rispettati: fallback a cursore statico, senza clamp e senza riduzione FPS automatica.
- Priorità risorse: framebuffer > cursore statico > cursore animato.

---

## Mappatura lato app_server

I campi del manifesto testo vengono mappati in una descriptor struct lato `app_server`, poi applicati a `ServerCursor` (es. tramite `ConfigureAnimation(...)` e frame upload in memoria cursore).

Questo consente un modello unico per:
- ingestion build-time (sistema),
- ingestion runtime (custom, fase 2).

---

## Piano pratico di implementazione

1. **Definizione formato canonico**
   - schema cartelle `.frames`;
   - formato manifesto testuale;
   - regole di validazione (dimensioni, formato, hotspot coerenti).

2. **Generatore build-time**
   - tool che legge:
     - file base cursore,
     - frame in `.frames/`,
     - manifesto testo,
   - e genera dati compilabili consumati da `app_server`.

3. **Integrazione `app_server` (fase 1)**
   - sostituire l’assunzione “solo embedded statico” con descriptor generati (base + animazione);
   - inizializzare i `ServerCursor` di sistema con i campi manifesto.

4. **Fase 2 custom runtime (API pubblica)**
   - estendere `BCursor` per trasporto frame+campi animazione;
   - estendere protocollo app_server per payload animato;
   - costruire `ServerCursor` animato lato server con stessi campi canonici.

5. **Validazione**
   - parsing manifesto (ok/errori);
   - fallback statico su vincoli invalidi o limiti hardware;
   - assenza regressioni su cursori statici e su driver senza supporto animazione.

---

## Criteri di accettazione

- Nessun fallback software animato.
- Nessuna animazione parziale/troncata.
- Nessuna regressione per app che usano `BCursor` statico.
- Cursori di sistema animati caricati da pipeline build con manifesto testo.
- Percorso custom runtime animato disponibile in fase 2 con API pubblica.
