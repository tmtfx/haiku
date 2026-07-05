# Haiku USB Stack — Roadmap di Stabilizzazione e Modernizzazione

> **Scopo.** Documento vivo per tracciare, in ordine di priorità, il lavoro sullo
> stack USB: **prima STABILE, poi moderno**. È la fonte di verità del progresso.
> Nasce dalla revisione critica del 2026‑07 (core, EHCI/XHCI, OHCI/UHCI, `usb_raw`,
> gap architetturali).

## Come si usa (convenzioni)

- **ID stabile** per ogni voce, es. `USB-STAB-01`. Non riusare mai un ID.
- **Stato**: `[ ]` da fare · `[~]` in corso · `[x]` fatto e verificato · `[!]` bloccato.
- Quando chiudi una voce, compila `Commit:` (hash breve) e `Verificato:` (come).
- **Convenzione di commit** (rende `git log` il diario dei progressi):
  `USB: [USB-STAB-01] breve descrizione` — un ID per commit quando possibile.
  Filtro rapido: `git log --oneline --grep "\[USB-"`.
- **Un branch per fase** (es. `usb/stab`, `usb/robust`) che confluisce quando la
  fase è verde. Le fasi si affrontano in ordine: non aprire P2 con P0 rossa.
- **Criterio di uscita di fase**: tutte le voci `[x]` + nessuna regressione nei
  test della fase corrispondente (vedi FASE 2).
- Contesto cross‑sessione: memoria `haiku-usb-known-defects`, `usb-isochronous-wip`.

Legenda gravità: **C**ritica · **A**lta · **M**edia · **B**assa.

---

## FASE 0 — Stabilizzazione: bug confermati, basso rischio, alta confidenza

> Obiettivo: eliminare i crash/hang certi senza cambiare architettura. Ogni voce è
> stata verificata leggendo il sorgente.

- [x] **USB-STAB-01** (A) — `set_pipe_policy` casta un `bool` a puntatore.
  - File: `src/add-ons/kernel/bus_managers/usb/usb.cpp:514`
  - Fix: `(IsochronousPipe *)object.IsSet()` → `(IsochronousPipe *)object.Get()`.
  - Accettazione: `set_pipe_policy` su pipe isocrona non causa crash; smoke test iso.
  - Commit: a3644135 · Verificato: revisione codice; build/test in FASE 2
- [x] **USB-STAB-02** (M) — Leak di `fMemoryWaitersCount` su timeout in `Allocate`.
  - File: `src/add-ons/kernel/bus_managers/usb/PhysicalMemoryAllocator.cpp:206–220`
  - Fix: decrementare il contatore anche sul percorso `B_TIMED_OUT` prima del `break`.
  - Accettazione: sotto pressione di memoria il contatore torna a 0; niente `NotifyAll` spurii.
  - Commit: f3863cda · Verificato: revisione codice; build/test in FASE 2
- [x] **USB-STAB-03** (M) — TOCTOU nel double‑free check di `Deallocate`.
  - File: `src/add-ons/kernel/bus_managers/usb/PhysicalMemoryAllocator.cpp:268` (lettura fuori da `fLock` acquisito a 273)
  - Fix: spostare il controllo `fArray[..][index]==0` dentro la sezione bloccata.
  - Accettazione: nessuna corruzione con alloc/free concorrenti (stress test).
  - Commit: 62c3e08d · Verificato: revisione codice; build/test in FASE 2
- [~] **USB-STAB-04** (A) — Refcount letto/free fuori lock in `usb_raw_device_removed`.
  - File: `src/add-ons/kernel/drivers/bus/usb/usb_raw.cpp:126–134`
  - Fix: spostato `device->device = 0` e il free dentro `gDeviceListLock` (dove open/free
    aggiornano `reference_count`), chiudendo la race sul device a metà rimozione.
  - Accettazione: unplug durante ioctl attivo non produce UAF (test hotplug ripetuto).
  - Commit: nel working tree, NON committato (file con tuo WIP isocrono) · Verificato: revisione codice
- [x] **USB-STAB-05** (M) — Parsing config descriptor: over‑read di 1 byte e hang su `length==0`.
  - File: `src/add-ons/kernel/bus_managers/usb/Device.cpp:141–142` (+ avanzamento in coda al loop)
  - Fix: validare `descriptorStart+2 <= actualLength` prima di leggere il tipo; se il
    campo `length` è 0 abortire il parse invece di ciclare.
  - Accettazione: descrittore malformato (length 0 / troncato) → errore pulito, niente hang.
  - Commit: 5fe1f7fd · Verificato: revisione codice; build/test in FASE 2
- [x] **USB-STAB-06** (M) — Guardie mancanti contro `packet_count == 0` nei percorsi isocroni.
  - File: `src/add-ons/kernel/busses/usb/uhci.cpp:1258` (OHCI aveva già la guardia a `ohci.cpp:2156`)
  - Fix: rifiutare `packet_count == 0` a monte in `UHCI::SubmitIsochronous`.
  - Accettazione: ioctl isocrono con `packet_count=0` → `B_BAD_VALUE`, niente div‑by‑zero.
  - Commit: d98d5f2f · Verificato: revisione codice; build/test in FASE 2
- [~] **USB-STAB-07** (M) — `fHostSystemError` è `volatile bool` senza barriere.
  - File: `src/add-ons/kernel/busses/usb/ehci.h:234`, `ehci.cpp:1621`/`2141`
  - Fix: `int32` + `atomic_set/atomic_get` per la visibilità cross‑CPU.
  - Accettazione: il finisher vede il flag in modo affidabile; nessun accesso a iTD post‑HSE.
  - Commit: nel working tree, NON committato (file con tuo WIP isocrono) · Verificato: revisione codice

**Uscita FASE 0:** tutte `[x]`; nessun panico/hang nei percorsi control/bulk/interrupt/iso di base.

---

## FASE 1 — Robustezza & recovery: rischio medio, cambi contenuti

> Obiettivo: rendere lo stack resistente a errori hardware, unplug e device ostili.

- [ ] **USB-ROB-01** (A) — HSE = vero recovery, non solo log.
  - File: EHCI `ehci.cpp:1619`, XHCI `xhci.cpp:2533`, OHCI `ohci.cpp:973`
  - Fix: su Host System Error fermare il controller, fallire i transfer in volo con
    stato d'errore, reset e re‑setup; notificare lo stack.
  - Accettazione: HSE iniettato → transfer completano con errore, bus si riprende.
- [ ] **USB-ROB-02** (M) — Chiudere l'anello `COMP_MISSED_SERVICE` isocrono in XHCI.
  - File: `src/add-ons/kernel/busses/usb/xhci.cpp` (HandleTransferComplete)
  - Fix: propagare lo stato ai packet descriptor isocroni (pacchetto vuoto marcato),
    non lasciarli non inizializzati.
- [ ] **USB-ROB-03** (A) — EHCI split‑isochronous (SITD) per device FS dietro hub HS.
  - File: `src/add-ons/kernel/busses/usb/ehci.cpp:1191` (`SubmitIsochronous` crea solo ITD; SITD allocati a 682–691 ma non usati)
  - Fix: cablare creazione/scheduling SITD nel submit isocrono.
  - Accettazione: webcam/audio USB1.1 dietro hub USB2 streamano.
- [ ] **USB-ROB-04** (B) — Spinlock ISR per‑istanza invece di `static`.
  - File: `ehci.cpp:1575`, `ohci.cpp:~922`
  - Fix: membro `fInterruptLock` per controller.
- [ ] **USB-ROB-05** (M) — Limite di 8 figli nell'API di topologia.
  - File: `usb.cpp:597`, `usb.cpp:627`
  - Fix: iterare fino a `USB_MAX_PORT_COUNT` (255).
- [ ] **USB-ROB-06** (M) — Irrobustire cancellazione/rimozione transfer in tutti i controller.
  - Fix: garantire callback invocati e nessun DMA verso buffer liberati (verifica StopEndpoint/SetTRDequeue in XHCI).

**Uscita FASE 1:** stress hotplug + iniezione errori superati; nessun leak di descrittori.

---

## FASE 2 — Test & osservabilità: fondamenta prima di modernizzare

> Obiettivo: non aggiungere feature senza rete di sicurezza. Questa fase abilita
> la verifica di tutto il resto.

- [ ] **USB-TEST-01** (A) — Harness di test riproducibile (unit su parser descrittori/allocatore; integrazione su enumerazione).
- [ ] **USB-TEST-02** (M) — Tracing runtime tipo `usbmon` + contatori per‑device (oltre a `TRACE_USB` compile‑time).
- [ ] **USB-TEST-03** (M) — Fuzzing dell'interfaccia ioctl di `usb_raw` (input non fidati da userland).
- [ ] **USB-TEST-04** (B) — Deduplicare i root hub `ohci_rh.cpp`/`uhci_rh.cpp` (~95% identici) in un modulo comune.

**Uscita FASE 2:** i fix di FASE 0/1 hanno test di regressione; le fasi successive
sono verificabili automaticamente.

---

## FASE 3 — Modernizzazione: grandi feature, richiedono evoluzione API

> Obiettivo: colmare i gap che l'utente 2020 sente per primo. Prerequisito
> trasversale: **API v4** con negoziazione delle capacità (l'attuale `USB3.h` è
> congelata e non ha spazio per streams/PM/Type‑C senza rottura ABI).

- [ ] **USB-MOD-00** (A) — Progettare `usb_module_info` v4 estensibile (streams, PM, Type‑C) mantenendo lo shim v2/v3.
  - File di riferimento: `headers/os/drivers/USB3.h`
- [ ] **USB-MOD-01** (A) — Bulk streams in XHCI + supporto **UAS** in `usb_disk`.
  - File: `xhci.cpp` (nessun `MaxPStreams`; doorbell `STREAMID(0)` a :2564), `usb_disk.cpp` (solo BOT: CBW/CSW)
  - Impatto: prestazioni storage esterno.
- [ ] **USB-MOD-02** (A) — Power management: suspend/resume/selective‑suspend/remote‑wakeup nell'API, suspend controller su S3, abilitare U1/U2 (`xhci.cpp:1608`) e LPM USB2.
- [ ] **USB-MOD-03** (A) — USB‑C / Power Delivery / Alt‑mode: nuovo sottosistema TCPM/TCPC (dock, caricatori, DP‑alt, billboard).
- [ ] **USB-MOD-04** (M) — Correttezza SuperSpeed+: parsare companion isocrono SS+ (`xhci.cpp:2258`) e onorare `Mult`/burst (`xhci.cpp:1029`).
- [ ] **USB-MOD-05** (M) — Multi‑interrupter xHCI (MSI‑X > 1, event ring per‑CPU) — oggi un solo vettore (`xhci.cpp:535`).
- [ ] **USB-MOD-06** (M) — Nuovi driver di classe: MTP/PTP, CCID (smartcard), DFU; promuovere UVC a percorso robusto.
- [ ] **USB-MOD-07** (B) — USB4/Thunderbolt (router/tunneling) — grande, bassa frequenza d'uso.

---

## Log delle decisioni (append‑only)

> Annota qui scelte architetturali non ovvie (perché, alternative scartate), con data.

- 2026‑07‑05 — Creato il tracker; ordine imposto stabilità → robustezza → test → modernità.
