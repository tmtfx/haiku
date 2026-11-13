# Piano di implementazione: cursori animati hardware-only in Haiku

## Obiettivo

Implementare cursori animati **solo** sui driver che supportano il cursore hardware, evitando carico CPU non necessario e mantenendo comportamento robusto su GPU vecchie (anche con VRAM ridotta).

---

## Requisiti e vincoli

- Nessun fallback software animato: se la scheda/driver non supporta la feature, il cursore resta statico.
- Animazione senza degradazioni qualitative (niente clamp/troncamenti in playback, niente riduzione FPS automatica).
- Priorità assoluta al framebuffer: se manca memoria, disabilitare animazione e mantenere cursore statico.
- Compatibilità con hardware legacy (es. buffer cursore piccoli come 1 KB).

---

## Formato dati del cursore (metadati)

### Strategia metadati

Non usare un unico attributo `frames[]` aggregato: usare invece **un metadato per frame** per evitare rischi legati a limiti dimensionali degli attributi.

### Metadati proposti

- `cursor_ani_frame_count` (uint32): numero frame animati disponibili (escluso frame base bitmap).
- `cursor_ani_start_delay_ms` (uint32): ritardo di avvio animazione dopo attivazione condizione.
- `cursor_ani_idle_delay_ms` (uint32): ritardo inattività prima dell’avvio (se trigger idle).
- `cursor_ani_speed` (uint32 o float): fattore velocità base (opzionale, sovrascrivibile da preflet).
- `cursor_ani_trigger` (enum/int): `0=immediato`, `1=on_idle`.
- `cursor_ani_frame_01 ... cursor_ani_frame_15`: payload bitmap per i frame aggiuntivi.

Note:
- `frame 0` è la bitmap cursore standard (già fornita dal flusso attuale, non negli attributi animati).
- Con massimo 16 frame totali, i frame aggiuntivi sono al massimo 15.
- Tutti i frame devono avere stesso formato/dimensioni/hotspot del frame base.
- La presenza di `cursor_ani_frame_count` implica che il cursore dichiara una variante animata (non serve `cursor_ani_enabled`).

---

## Estensione API accelerant/hook

### Hook nuovi

Estendere la sezione cursor management con hook opzionali:

- `B_START_CURSOR_ANI`
- `B_STOP_CURSOR_ANI`
- `B_SET_CURSOR_ANI_SPEED`
- `B_CURSOR_SUPPORTED_FRAMES` (numero massimo di frame aggiuntivi gestiti dalla scheda/driver)
- `B_SUPPORTS_CURSOR_ANI` (**obbligatorio**)

`B_SUPPORTS_CURSOR_ANI` deve essere consultabile anche rispetto alla configurazione utente
(`.settings`), così da poter ritornare `false` quando l’animazione hardware è disabilitata
come policy/predefinito.

### Ruolo di `B_SET_CURSOR_BITMAP`

`B_SET_CURSOR_BITMAP` resta il punto unico di caricamento e validazione:

1. verifica supporto animazione tramite `B_SUPPORTS_CURSOR_ANI` (hook richiesto);
2. legge metadati;
3. legge la capacità hardware via `B_CURSOR_SUPPORTED_FRAMES`;
4. verifica che `cursor_ani_frame_count` sia <= capacità driver/hardware;
5. valida coerenza frame;
6. copia nei buffer VRAM preallocati;
7. prepara stato animazione.

Se qualunque passaggio fallisce: **fallback statico** immediato.

---

## Policy frame/memoria

### Scelta principale

- `MAX_CURSOR_ANI_FRAMES = 16` (totali, incluso frame base).
- `frame_count` dichiarato dal cursore deve essere `1..16`.
- `B_CURSOR_SUPPORTED_FRAMES` esprime il massimo numero di frame aggiuntivi supportati dal driver.
- Validazione finale: `frame_count - 1 <= B_CURSOR_SUPPORTED_FRAMES`.
- Se il contenuto non entra nel budget hardware: **statico** (senza clamp frame, senza riduzione FPS).

### Motivazione

Questa policy evita animazioni “rotte” o tronche: o l’animazione è completa e corretta, o non parte.

---

## Allocazione VRAM: preallocazione statica

### Principio

Il driver non deve allocare dinamicamente buffer durante `B_SET_CURSOR_BITMAP`.  
Preallocare all’inizializzazione i buffer addizionali per i frame animati.

### Formula

`extra_bytes = (MAX_CURSOR_ANI_FRAMES - 1) * cursor_buffer_bytes`

dove `cursor_buffer_bytes` è il buffer reale del cursore per quella scheda (non hardcodato a 1 KB).

### Comportamento

- Se preallocazione fallisce: supporto animazione disabilitato, cursore statico.
- Se preallocazione riesce ma un cursore specifico non rispetta i vincoli: quel cursore resta statico.
- Se un cursore richiede più frame della capacità dichiarata da `B_CURSOR_SUPPORTED_FRAMES`: fallback statico.
- Buffer inutilizzati restano allocati ma inattivi (trade-off accettato per robustezza e semplicità).

---

## Scheduling animazione in app_server

### Scelta

Lo scheduling dei frame viene gestito da `app_server` (timer leggero), con driver che esegue solo switch del frame già in VRAM tramite hook.

### Gestione cambio cursore durante animazione

Usare `generation_id` (o token equivalente):

1. stop animazione corrente;
2. incrementa `generation_id`;
3. invalida timer/eventi vecchi;
4. carica nuovo cursore;
5. avvia nuova animazione (se valida e abilitata).

Questo evita race/stale events nel passaggio rapido tra cursori.

---

## Preflet Screen (configurazione utente)

Integrare nella stessa area del cursore hardware:

- checkbox: **Abilita animazioni cursore hardware**
- slider: **Velocità animazione**
- slider/spinbox: **Ritardo inattività (ms)**

Comportamento UI:

- controlli attivi solo se cursore hardware è abilitato;
- controlli animazione attivi solo se `B_SUPPORTS_CURSOR_ANI == true`.
- lo stato salvato in `.settings` può forzare `B_SUPPORTS_CURSOR_ANI == false` come default/policy.

---

## Fallback/memoria (policy operativa)

Ordine di priorità:

1. framebuffer / modalità video;
2. cursore hardware statico;
3. cursore hardware animato.

Se risorse insufficienti per l’animazione, la feature viene spenta senza impattare stabilità o rendering principale.

---

## Piano pratico di rollout

1. **Fase 1: API + infrastruttura app_server**
   - aggiunta hook in `accelerant.h`;
   - wiring capability;
   - scheduler base con generation guard.

2. **Fase 2: Driver pilota Kyro (buffer cursor 1 KB)**
   - preallocazione frame buffers;
   - implementazione `B_CURSOR_SUPPORTED_FRAMES` coerente con limiti reali della scheda;
   - implementazione `B_SET_CURSOR_BITMAP` con parsing metadati;
   - implementazione start/stop/speed.

3. **Fase 3: Preflet Screen**
   - checkbox animazioni;
   - slider velocità;
   - ritardo inattività;
   - gating su capability driver.

4. **Fase 4: Test funzionali e negativi**
   - cursore statico (nessun metadato);
   - cursore animato valido (frame_count <= 16);
   - frame_count invalido;
   - frame eterogenei (size/format/hotspot mismatch);
   - memoria insufficiente (fallback statico).

5. **Fase 5: Aggiornamento asset cursori**
   - aggiunta metadati animazione ai cursori selezionati (attività separata).

---

## Criteri di accettazione

- Nessun aumento sensibile CPU in idle/mouse move rispetto a cursore statico hardware.
- Nessun crash o race nel cambio cursore rapido.
- Nessun rendering corrotto quando animazione non disponibile.
- Nessuna animazione parziale/troncata: solo completa o statica.
- Feature disattivabile globalmente da preflet.

