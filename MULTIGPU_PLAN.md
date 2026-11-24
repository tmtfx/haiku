# Multi-GPU: righe di workspace assegnate per scheda video

Ramo: `MultiGPU` (repo `/boot/home/Progjets/haiku`). Documento di progetto completo: contesto, decisioni prese, stato del codice verificato, piano a fasi, rischi trovati durante la validazione, file critici, strategia di verifica.

## Stato di implementazione (aggiornato dopo la sessione di codifica)

**Implementate e build-verificate (`app_server` + `Screen` compilano senza errori):**
- **Fase B**: confermato nessun lavoro necessario.
- **Fase C**: `ScreenManager::_ScanDrivers()` enumera tutte le schede (`collect_graphics_device_paths()`), un `AccelerantHWInterface` per scheda (nuovo `Initialize(const char* devicePath)`); `AccelerantHWInterface::GetPciLocation()` aggiunto.
- **Fase D**: `VirtualScreen::SetActiveScreen()`, `Desktop::_UpdateActiveGpuScreens()`/`fActiveGpuScreens`, mappatura riga→GPU in `_SetCurrentWorkspaceConfiguration()`, sweep DPMS.
- **Fase E**: lato server fatto (`AllActiveGpusHaveHardwareCursor()`); lato client (gate multi-schermo nel preflet) non ancora esteso oltre lo schermo 0.
- **Fase F**: protocollo `AppServer.cpp` esteso con `"screen"` su tutti i setter/getter pertinenti + nuovi `'cnts'`/`'devn'`; `ScreenSettings` riscritta su `BMessage` per-schermo; `ScreenMode`/`MonitorView` parametrizzati per indice schermo; `ScreenWindow` riorganizzata in `BTabView` (tab "Workspaces" con righe/colonne + `WorkspaceGridView` nuovo + checkbox compositor; tab per lo schermo 0 = controlli originali intatti; tabs per schermi 1..N-1 = nuova `GPUSettingsView`, un sottoinsieme più semplice ad applicazione immediata, senza Apply/Revert/undo).

**Bug preesistenti trovati e corretti durante l'implementazione (non erano nel piano originale):**
- `VirtualScreen::ScreenByID()` aveva un OR con `B_MAIN_SCREEN_ID.id` che faceva risolvere QUALSIASI lookup con id=0 (cioè ogni `BScreen(B_MAIN_SCREEN_ID)` da qualunque app) all'ultimo schermo della lista invece che a quello con ID realmente 0, non appena c'è più di uno schermo.
- In `ServerApp.cpp`, ~10 gestori del protocollo `BPrivate::ScreenProtocol` (`AS_PROPOSE_MODE`, `AS_GET_MODE_LIST`, `AS_GET_ACCELERANT_INFO`, `AS_GET_MONITOR_INFO`, `AS_GET_RETRACE_SEMAPHORE`, `AS_GET_TIMING_CONSTRAINTS`, `AS_GET_PIXEL_CLOCK_LIMITS`, `AS_SET_DPMS`, `AS_GET_DPMS_STATE`, `AS_GET_DPMS_CAPABILITIES`) ignoravano completamente lo `screen_id` ricevuto e operavano sempre su `fDesktop->HWInterface()` (lo schermo attivo), non sullo schermo richiesto — commenti espliciti "We aren't using the screen_id for now...". Corretti con un nuovo helper `get_hw_interface_for_screen()`. `AS_VALID_SCREEN_ID` accettava solo id 0; `AS_GET_NEXT_SCREEN_ID` era hardcoded "siamo sempre l'ultimo schermo". Anche `Desktop::SetBrightness()` operava sempre sullo schermo attivo indipendentemente dall'id richiesto — corretto.

**Fase A — rivista: niente syscall kernel, ordine GPU configurato manualmente dall'utente.** L'idea originale (enumerazione PCI dal boot loader + rilevamento primaria via BAR, fatta arrivare ad app_server tramite `kernel_args`/`add_boot_item`) si è rivelata non realizzabile come pensata: `get_boot_item()` è **kernel-only**, app_server (userspace) non può chiamarlo. Un'alternativa più leggera (un nuovo, piccola syscall per esporre `FRAME_BUFFER_BOOT_INFO`, confrontato con `frame_buffer_dma` già disponibile via `GetFrameBufferConfig()`) avrebbe funzionato, ma è l'unico pezzo di questa feature che toccherebbe davvero il kernel — non verificabile senza un boot reale, quindi rischio diverso da tutto il resto del progetto. **Decisione presa con l'utente**: niente modifiche kernel. L'ordine/la primaria si configurano manualmente dal preflet e si persistono in `Screen_data`:
- `ScreenSettings::GpuOrder()`/`SetGpuOrder()` — elenco ordinato di BDF (`"bbddff"` hex), primo = primaria.
- `Desktop::_UpdateActiveGpuScreens()` applica quest'ordine (schede elencate per prime, nell'ordine dato; le altre restano nell'ordine di scoperta) — fallback sicuro a "ordine di scoperta" se non configurato.
- Nuovo messaggio `AS_SET_GPU_ORDER`; `Desktop::GpuConfigurationChanged()` (rinominato da `ScreenEDIDChanged()`, ora generico) ricalcola su entrambi i trigger (cambio EDID o cambio ordine).
- Nuovo messaggio `'gbdf'` in `AppServer.cpp` per esporre il BDF di uno schermo al preflet.
- **UI** nella tab "Workspaces": box "GPU order (first = primary)" con `BListView` + bottoni "Move up"/"Move down", che scrivono l'ordine via `SetGpuOrder()` e aggiornano subito l'anteprima `WorkspaceGridView`. Logica di calcolo dell'ordine effettivo (`ScreenWindow::_GetEffectiveGpuOrder()`) condivisa tra l'anteprima e la lista di riordino, specchiando esattamente quello che fa `Desktop` lato server.

**Bug aggiuntivo trovato e corretto durante il test utente (non nel piano)**: `BTab::SetLabel()` è no-op se chiamato prima di `AddTab()` (richiede `fView` già impostato, cosa che avviene solo dentro `AddTab()`). Il codice chiamava `SetLabel()` prima — risultato: tutte le etichette delle tab erano vuote, e con `B_WIDTH_FROM_WIDEST` la tab view sembrava avere una sola tab cliccabile. Confermato e risolto dall'utente su hardware reale.

**Confermato su hardware reale (2026, test utente)**: build single-GPU, nessuna regressione — layout a tab, workspace grid, GPU order box tutti funzionanti su macchina a una sola scheda.

**Non ancora fatto / gap noti, elencati con l'utente dopo il test single-GPU:**

1. **Gate del compositor incompleto per multi-GPU** (in correzione in questa stessa sessione, vedi sotto): `AS_SET_2D_COMPOSITOR_ENABLED` (il percorso di attivazione *live*, cioè il click sul checkbox) non ha **alcun** gate server-side — solo il ripristino al boot in `Desktop::Init()` controlla `AllActiveGpusHaveHardwareCursor()`. Lato client, `ScreenWindow.cpp` decide se abilitare il checkbox guardando solo la capacità della scheda 0, non di tutte le GPU attive.

2. **Decoupling architetturale non risolto tra "GPU order" e `B_MAIN_SCREEN_ID`**: l'ordine configurato dall'utente (Fase A rivista) decide solo quale scheda guida la riga 0 dei workspace in `Desktop::fActiveGpuScreens` — **non cambia `Screen::ID()`**, assegnato una volta per tutte da `ScreenManager` nell'ordine di scoperta al boot. Qualsiasi app generica che usa `B_MAIN_SCREEN_ID` (id implicito 0) continua a risolvere sullo schermo scoperto per primo, non su quello configurato come primario, se i due ordini divergono. Non risolto: richiederebbe toccare un ID usato come chiave stabile per le `ScreenConfigurations` salvate per workspace — cambiamento più delicato, da pianificare separatamente, non incluso in questa sessione.

3. **Nessuna gestione "scheda senza EDID" nel preflet finché non assegnato**: i controlli di risoluzione/colore/refresh/cursore-hardware/cursori-animati/2D non si disabilitano automaticamente quando una scheda non ha un EDID valido (né da `.settings` né da Load EDID di sessione). L'utente si aspettava che questi controlli restassero disabilitati finché non viene assegnato un EDID.

4. **Nessun modo per "togliere" un EDID caricato a runtime** (tasto "Unset EDID", pensato per le sole schede secondarie): oggi si può solo assegnarne uno (Load EDID, volatile per la sessione) o impostarne uno stabile via file `.settings` del driver (manuale, fuori dal preflet) — non c'è un percorso nel preflet per tornare allo stato "nessun EDID" una volta caricato.

5. **Nessun modo dal preflet per rendere "stabile" (persistente al riavvio) un EDID caricato a runtime**: Load EDID è per forza volatile (vive solo nello shared_info del driver in RAM). Serve un meccanismo per scrivere l'EDID su disco e far sì che il driver lo recuperi anche dopo un riavvio.

I punti 3-5 sono collegati e trattati insieme nella sessione corrente — design e implementazione qui sotto.

## Punto 1 — RISOLTO: gate del compositor completo

- `Desktop::SetCompositor2DEnabled()` ora rifiuta `enable=true` se `!AllActiveGpusHaveHardwareCursor()`, indipendentemente da chi chiama (live toggle o altro) — non solo il ripristino al boot.
- `ScreenWindow.cpp`: costruzione del checkbox e gestore `TOGGLE_HARDWARE_CURSOR_MSG` ora usano `_AllActiveGpusHaveHardwareCursor()` (nuovo metodo, interroga `'cnts'`+`'hedi'`+`'hsbm'` per ogni schermo effettivamente attivo), non più solo la capacità dello schermo 0.

## Punti 3-5 — RISOLTI: gestione EDID (stato/unset/stabile)

**Decisione presa**: niente modifica automatica dei file `.settings` dei driver. Verificato che il nome file non è uniforme tra driver (`"intel_extreme"` senza suffisso, `"ati.settings"`/`"3dfx.settings"` con suffisso) — scrivere al posto giusto richiederebbe indovinare la convenzione per ogni driver, con rischio di fallire silenziosamente scrivendo nel file sbagliato (e scoprirlo solo dopo un riavvio). Scelto un approccio **assistito**: il preflet salva il file EDID in un percorso noto e mostra all'utente la riga esatta da aggiungere, ma non edita il `.settings` per lui.

**Correzione importante (dopo il test utente)**: la prima versione gatava *qualunque* schermo senza EDID, primario incluso — sbagliato. Caso reale che lo dimostra: una Matrox che non implementa affatto `GetEDID()` (nessun supporto Dump/Load, infatti i relativi tasti sono già disabilitati per capacità) ma dialoga comunque in DDC con il monitor per la lista modi — se fosse l'unica scheda, la vecchia logica l'avrebbe messa in DPMS off, spegnendo l'unico schermo del sistema. **Regola corretta**: la GPU primaria (l'ordine configurato in Fase A, primo elemento; in assenza di configurazione, la prima scoperta — stessa convenzione di `determine_primary_bdf()`) è **sempre** considerata attiva/EDID-valida, qualunque cosa riporti `GetEDID()` — solo le GPU secondarie vengono gatate per EDID reale. La logica vive **solo lato server** (`Desktop::_UpdateActiveGpuScreens()` + l'helper `is_primary_screen()`/`get_screen_bdf()` duplicato in `AppServer.cpp` per il messaggio `'hedi'`); il codice del preflet non è cambiato — si limita a interrogare `'hedi'` e si comporta già correttamente una volta che il server risponde il valore giusto.

**Stato effettivo EDID** (`'hedi'`, messaggio in `AppServer.cpp`): per la GPU primaria sempre vero; per le secondarie, combina due condizioni — il driver riporta un EDID valido via `GetEDID()` (DDC, boot item, o override `.settings`) **e** l'utente non ha forzato lo stato "unset" per quella scheda. Usato sia da `Desktop::_UpdateActiveGpuScreens()` (server, per DPMS/riga) sia dal preflet (client, per abilitare/disabilitare i controlli) — stessa logica in entrambi i posti, nessuna divergenza possibile.

- **Blocco controlli senza EDID (solo secondarie, grazie all'esenzione della primaria sopra)**: `ScreenWindow::_RefreshEdidGatedControls()` (schermo 0, tipicamente primario) e `GPUSettingsView::_RefreshEdidGatedControls()` (schermi secondari) disabilitano risoluzione/colori/refresh/combine/swap/pannello-laptop/standard-TV/luminosità/cursore-hardware/cursori-animati/2D quando `_QueryHasEDID()` è falso. Dump/Load EDID restano sempre abilitati per capacità (indipendentemente dall'EDID — su Matrox sono già disabilitati perché il driver non espone gli hook, non per mancanza di EDID). Richiamato alla costruzione e dopo ogni Load/Unset EDID.
- **Nota residua (collegata al gap #2)**: se l'utente riordina le GPU rendendo primaria una scheda diversa dall'indice 0, la tab che `ScreenWindow` mostra resta comunque fissata sullo schermo 0 (identità per indice di scoperta, non per ruolo) — quindi lato server la nuova primaria viene correttamente esentata, ma la tab "semplificata" (`GPUSettingsView`) di quella scheda non acquisisce i controlli completi (combine/swap/Apply-Revert) che la tab fissa 0 ha. Il tasto "Unset EDID" su una scheda diventata primaria per riordino risulterebbe inerte (il server la esenta comunque) — non pericoloso, solo un angolo noto non raffinato.
- **"Unset EDID"** (solo schermi secondari, nuovo tasto in `GPUSettingsView`): persiste un flag `"edidunset_<bdf>"` in `Screen_data` (`ScreenSettings::EdidUnset()`/`SetEdidUnset()`, keyed per BDF come `GpuOrder()`), notifica app_server (`AS_GPU_CONFIG_CHANGED`, rinominato da `AS_SET_GPU_ORDER` perché ora serve a entrambi i trigger), che fa ricalcolare a `Desktop::_UpdateActiveGpuScreens()` — la scheda torna EDID-less (DPMS off, esclusa dalla rotazione righe) indipendentemente da cosa riporta realmente il driver.
- **"Rendi permanente"** (`_OfferToSaveStableEdid()`, in entrambe le view, dopo un Load EDID riuscito): un `BAlert` chiede se rendere l'EDID appena caricato permanente; se sì, salva il blob in `~/config/settings/edid/<bdf>.bin` (percorso certo, deciso da noi) e mostra un secondo `BAlert` con il percorso e la riga `edid <path>` da aggiungere manualmente al file `.settings` del driver giusto — l'utente la aggiunge lui stesso, nessun rischio di scrivere nel file sbagliato.
- Caricare un nuovo EDID pulisce automaticamente un eventuale "unset" precedente per quella stessa scheda (riassegnazione esplicita).

## Contesto e obiettivo

Oggi Haiku (e questo fork) gestisce esattamente **una** scheda video: `ScreenManager::_ScanDrivers()` carica un solo `AccelerantHWInterface` ("For now, we'll just load one and be done with it") e `VirtualScreen` è costruito per un solo schermo attivo alla volta (commento nel codice: "TODO: can we have a multiplexing HWInterface as well?").

Obiettivo: al boot, rilevare tutte le schede video installate e identificare la primaria (quella che ha fatto il POST ed è visibile sul boot loader); in app_server, assegnare ogni **riga** della griglia dei workspace a una scheda video attiva, ciclicamente: `row -> activeGpuList[row % N]`, dove `activeGpuList` è l'elenco ordinato (primaria in testa) delle sole schede con un EDID valido (runtime o `.settings`). Con 1 sola scheda attiva tutte le righe le appartengono (comportamento identico a oggi). Una scheda senza EDID è esclusa dalla rotazione e deve restare in `DPMS_OFF` (nessun segnale video). Il preflet Screen va riorganizzato in una `BTabView`: una tab "Workspaces" (righe/colonne + anteprima grafica a griglia raggruppata per scheda + interruttore compositor) e una tab per ogni scheda rilevata (tutte le impostazioni per-scheda di oggi, più uno spinner di risoluzione per ogni riga posseduta).

Progetto distinto da `MultiGPUActivation` (quel ramo mira a far emettere l'EDID dalle secondarie via POST firmware emulato — qui non serve, perché l'EDID arriva già da runtime/`.settings`).

### Decisioni prese con l'utente

- **EFI e BIOS legacy entrambi in scope** per l'enumerazione al boot (non solo EFI).
- Dal ramo `MultiGPUActivation` si porta **solo la parte di enumerazione PCI** (`efi_enumerate_display_devices()`), **non** il POST/harvesting EDID (`vga_rom_post`, `boot_edid`, `legacy_vga_rom`).
- **Fuori scope** (lavoro futuro): due schede che condividono lo stesso modulo driver (es. due Matrox: G450 + Millennium I, o due S3). Per ora ogni scheda nella rotazione multi-GPU usa un driver diverso.
- I workspace restano pienamente indipendenti/non sovrapponibili (comportamento stock Haiku, confermato già vero oggi — nessuna modifica necessaria su questo punto).

## Discussione a parte: il compositor 2D (Moeca)

Domanda dell'utente: il compositor può essere attivato in modo indipendente per scheda, o è tutto-o-niente? E se è tutto-o-niente, l'attivazione richiede il cursore hardware su tutte le schede?

Verificato nel codice: **sì a entrambe**.

- `fCompositor2DEnabled` è un membro di `Desktop` (`Desktop.cpp:452`), non di `Screen`/`HWInterface`. `Desktop::SetCompositor2DEnabled()` (riga 2276) quando cambia stato fa `LockAllWindows()` e itera **tutte** le finestre del sistema (`fAllWindows`) per allocare/liberare la backing store. Non esiste alcun concetto di "per schermo". È quindi necessariamente un interruttore globale per tutto il desktop (tutti i workspace, tutte le righe/schede): o acceso per tutto, o spento per tutto.
- Il gate reale per abilitarlo (`Desktop.cpp:562-572`, commento esplicito: le vecchie condizioni RAM/2D-accel sono state rimosse perché "non riflettevano mai un requisito reale", ma `HWInterface()->HasSetCursorBitmap() && hw` è stato mantenuto perché è l'unico vero prerequisito) oggi controlla una sola interfaccia. Il motivo tecnico: il cursore software viene disegnato/ripristinato direttamente nel framebuffer con una logica draw/restore che non sa nulla delle dirty-region del compositor; se il compositor ricompone sotto un cursore software, lo stato di restore diventa stale e genera artefatti/scie. Il cursore hardware è un piano separato, estraneo al framebuffer, quindi non ha questo problema.
- **Conseguenza multi-GPU**: il gate deve diventare "tutte le GPU in `activeGpuList` hanno `HasSetCursorBitmap()` vero e cursore hardware abilitato" — se anche una sola scheda attiva non lo supporta, il compositor resta disabilitato globalmente.
- **Conseguenza UI**: il checkbox "Enable 2D compositor" va nella tab principale "Workspaces" (è un'impostazione di `Desktop`, non di `HWInterface`), non ripetuto nelle tab per-scheda. Le tab per-scheda mostrano solo il proprio checkbox "cursore hardware"; la tab principale può segnalare quale scheda specifica blocca il compositor quando il master toggle è forzato off.

Vedi anche le memoria di sessioni precedenti: [[project-moeca-compositor-status]], [[feedback-compositor-change-scoping]] — qualunque modifica a `Desktop`/compositing condiviso va sempre gated esplicitamente, mai come cambiamento implicito.

## Stato attuale verificato (riferimenti esatti nel codice)

### EDID — già cablato end-to-end
- `src/preferences/screen/ScreenWindow.cpp`: Dump/Load EDID → messaggi `'gedi'`/`'sedi'` verso app_server, **già con un campo `"screen"` int32** verso `gScreenManager->ScreenAt(screenIndex)`.
- `src/servers/app/AppServer.cpp`: gestori `'gedi'`/`'sedi'` chiamano `Screen::HWInterface()->GetEDID()/SetEDID()`.
- `headers/private/graphics/common/edid_override.h` (`load_edid_override()`): legge un file EDID da path specificato nel `.settings` del driver (chiave `edid`), usato da `intel_extreme`, `radeon_hd`, ecc. **Keyed per modulo driver**, non per dispositivo — coerente con l'esclusione "stesso driver" dallo scope (se mai venisse rimossa, andrebbe rivisto).

### `ScreenManager` — singola istanza hardcoded
`ScreenManager.h`: `BObjectList<screen_item, true> fScreenList` dove `screen_item = {ObjectDeleter<Screen> screen; ScreenOwner* owner; ObjectDeleter<HWInterfaceListener> listener;}`. Metodi: `ScreenAt`, `CountScreens`, `AcquireScreens`, `ReleaseScreens`, `ScreenChanged`, `_AddHWInterface`, `_ScanDrivers`.

`ScreenManager.cpp:192-213`, corpo esatto di `_ScanDrivers()`:
```cpp
void
ScreenManager::_ScanDrivers()
{
	HWInterface* interface = NULL;

	// Eventually we will loop through drivers until
	// one can't initialize in order to support multiple monitors.
	// For now, we'll just load one and be done with it.

#ifndef HAIKU_TARGET_PLATFORM_LIBBE_TEST
	bool initDrivers = true;
	while (initDrivers) {
		interface = new AccelerantHWInterface();
		_AddHWInterface(interface);
		initDrivers = false;
	}
#endif
}
```
`_AddHWInterface()` (216-249) crea `new Screen(interface, fScreenList.CountItems())` — **l'ID dello schermo è l'ordine di scoperta/readdir, non correlato a quale scheda sia fisicamente primaria.** `AcquireScreens()` (120-158) oggi crea una *nuova* interfaccia solo per un target remoto (`RemoteHWInterface`), mai per una seconda GPU locale.

### `VirtualScreen` — un solo schermo attivo, sovrascritto ad ogni chiamata
`VirtualScreen.cpp:112-162`, `AddScreen()` — ad ogni chiamata sovrascrive incondizionatamente:
```cpp
	// TODO: this works only for single screen configurations
	fDrawingEngine = screen->GetDrawingEngine();
	fHWInterface = screen->HWInterface();
	fFrame = screen->Frame();
```
`UpdateFrame()` (173-192) somma le larghezze di **tutti** gli schermi registrati in un'unica riga virtuale (nessun concetto di riga/colonna workspace).

`VirtualScreen::SetConfiguration()` (52-109) fa: salva le modalità precedenti → `_Reset()` (rilascia tutti gli schermi a `ScreenManager`, azzera `fHWInterface`/`fDrawingEngine`/`fFrame`) → `gScreenManager->AcquireScreens()` (riacquisisce tutti gli schermi attualmente non posseduti — cioè tutti, dopo il reset) → loop `AddScreen()` su ciascuno.

Questa funzione è chiamata da **`Desktop::_SetCurrentWorkspaceConfiguration()`** (`Desktop.cpp:4291-4320`) **ad ogni cambio workspace**:
```cpp
void
Desktop::_SetCurrentWorkspaceConfiguration()
{
	...
	fVirtualScreen.SetConfiguration(*this,
		fWorkspaces[fCurrentWorkspace].StoredScreenConfiguration(),
		fWorkspaces[fCurrentWorkspace].CurrentScreenConfiguration(),
		&changedScreens);
	...
}
```
**Rischio concreto trovato in validazione**: con N>1 schermi, questo ciclo "release tutto → reacquire tutto → AddScreen in loop" lascerebbe "attivo" qualunque schermo risulti ultimo nell'ordine restituito da `AcquireScreens()` — **indipendentemente** dalla nostra mappatura riga%N voluta. Non è un rischio teorico: è il punto esatto dove la Fase D deve agganciarsi esplicitamente, altrimenti il comportamento sarà non deterministico. Nota positiva: `Screen::SetMode()` fa già un `memcmp` e no-opera se la modalità non cambia, quindi il "tocca tutti gli schermi ad ogni switch" esistente resta economico anche con più GPU (non vale la pena ottimizzarlo via).

### `AccelerantHWInterface` — nessuna identità PCI memorizzata
`AccelerantHWInterface.cpp:356-422`, `Initialize()`: scandisce `/dev/graphics/*` via `_OpenGraphicsDevice(i)`/`_RecursiveScan(...)` e fa `break` al primo `_OpenAccelerant()` riuscito. Non memorizza il path né la posizione PCI come membro persistente (verificato leggendo l'intero elenco membri dell'header).

`GetDriverPath()` (1389-1406, via hook `B_GET_ACCELERANT_CLONE_INFO`) dà **già, gratis**, il path tipo `/dev/graphics/intel_extreme_00:02:0`. Convenzione di naming verificata su tutti i driver grafici:
- `intel_extreme`, `radeon_hd`, `intel_arc`, `sm750`, `ast`, `xgi`: `"graphics/<driver>_%02x%02x%02x"` → bus/device/function sono i 6 hex finali.
- `matrox`, `nvidia`, `ati`, `s3`, `trident`, `via`, `3dfx`, `kyro`, `neomagic`, `intel_810`, `skeleton`, `framebuffer`, `vesa`: tutti usano la macro `DEVICE_FORMAT "%04x_%04x_%02x%02x%02x"` (vendor_device_BDF), stessa convenzione dei 6 hex finali.
- **Eccezione: `virtio`** (`src/add-ons/kernel/drivers/graphics/virtio/*.cpp:854`): nome dispositivo `"graphics/virtio/%d"`, un indice incrementale senza BDF — driver moderno basato su device_manager, diverso dagli altri. Caso raro (principalmente VM) — da gestire con fallback esplicito "posizione PCI ignota, non correlabile alla primaria rilevata al boot".
- Fonte di verità indipendente già esistente per cross-check: la syscall generica `DEVICE_MANAGER_SYSCALLS` (usata da `listdev`, `src/bin/listdev/dm_wrapper.c:21-56`) permette di leggere `B_DEVICE_BUS`/`B_DEVICE_VENDOR_ID`/`B_DEVICE_ID` dal device tree del kernel senza nuovo codice.

### DPMS — hook generici già pronti, già per-istanza
`headers/os/add-ons/graphics/Accelerant.h:49-51,312-314`: `B_DPMS_CAPABILITIES`/`B_DPMS_MODE`/`B_SET_DPMS_MODE` + typedef `dpms_capabilities`/`dpms_mode`/`set_dpms_mode`.
`AccelerantHWInterface.cpp:1277/1289/1301`: `SetDPMSMode()`/`DPMSMode()`/`DPMSCapabilities()` già implementati, già metodi per-istanza. Oggi chiamati solo tramite l'unico `Desktop::HWInterface()` globale (`Desktop.cpp:532`, `ServerApp.cpp:3419/3433/3446`) — una volta che esistono N istanze di `AccelerantHWInterface`, basta chiamare il metodo direttamente su quella specifica, nessun nuovo hook kernel/accelerant necessario.

### Caricamento driver kernel — già funziona per schede di vendor diversi
`src/system/kernel/device_manager/legacy_drivers.cpp`: `probe_for_drivers()` (1180-1237) enumera **tutti** i file driver in una directory e chiama `try_drivers()` (1154-1177) → `legacy_driver_add()` → `init_hardware()`/`init_driver()` per **ciascuno**. Ogni driver si auto-filtra per vendor/device ID (es. `intel_extreme/driver.cpp`'s `get_next_intel_extreme()` filtra su `VENDOR_ID_INTEL`; `matrox/driver.c` filtra su `SupportedDevices[vendor]`). Nessun arbitraggio VGA/legacy-decode trovato da nessuna parte nel codice (grep su `vga_arbiter`/`vga.decode`/`legacy_decode`: nessun risultato rilevante). **Conclusione: zero modifiche al kernel necessarie per questa parte** — una scheda Intel + una Matrox vengono già entrambe riconosciute e pubblicate come nodi `/dev/graphics/*` separati oggi, senza alcun lavoro aggiuntivo.

### Protocollo app_server — solo EDID è già indicizzato per schermo
In `AppServer.cpp` (150-391): `'gedi'`/`'sedi'` portano già un `"screen"` int32. **`AS_SET_HW_CUR_BITMAP_ENABLED`, `AS_SET_CURSOR_ANI_SETTINGS`, `AS_SET_2D_ACCELERATION_ENABLED`, `AS_SET_2D_DEBUG_ENABLED`** (setter) e i getter `'hsbm'`/`'h2da'`/`'gedc'`/`'sedc'`/`'hcai'` sono invece **hardcoded su `ScreenAt(0)`**, senza alcun indice schermo nel `BMessage`. Visto che il design delle tab per-scheda richiede di impostare cursore hardware/2D-accel/animazione per scheda specifica, questi 4 mittenti (in `ScreenSettings.cpp`/`ScreenWindow.cpp`) + 9 gestori (in `AppServer.cpp`) vanno estesi con un campo `"screen"`, sul modello già stabilito da `'gedi'`/`'sedi'`.

### Impostazioni preflet — blob binario globale, letto in 3 punti indipendenti
`ScreenSettings.cpp`: `Screen_data` è un blob binario posizionale a campi fissi (`BPoint` + 6 bool + 2 `int32`), tutto globale, nessuna chiave per schermo da nessuna parte. **Letto indipendentemente in tre punti**: `ScreenSettings.cpp` stesso, `Desktop.cpp:547-577` (ripristino compositor al boot) e `AccelerantHWInterface.cpp:378-417` (ripristino cursore hardware/2D accel/animazione cursore). Un cambio di formato (proposto: passare a `BMessage` flattened, chiavi per-scheda via BDF) deve aggiornare **tutti e tre** in sincrono — facile da dimenticare perché i due letori lato app_server/driver non sono visibili guardando solo il lato preferences.

### Boot loader — zero enumerazione PCI su entrambi i path oggi
- EFI (`platform/efi/video.cpp`): prende il primo handle GOP restituito da `LocateProtocol`, zero awareness PCI.
- BIOS legacy (`platform/bios_ia32/video.cpp`, `vesa_init()`): interamente INT10h/VBE, zero awareness PCI. **Nessun accesso PCI esiste in alcun punto del boot loader non-EFI** (confermato via grep sull'intero albero `src/system/boot/`, zero risultati per `pci_info`/`class_base`/`PCI_display`).
- Sul ramo `MultiGPUActivation` (NON in questo branch), `src/system/boot/platform/efi/pci_video.cpp`:`efi_enumerate_display_devices()` fa: `LocateHandleBuffer(ByProtocol, &sPciIoGuid, ...)` per tutti gli handle `EFI_PCI_IO_PROTOCOL` → per ciascuno, `pciIo->Pci.Read(..., 0x0b /* class_base */, ...)` filtra `classBase == 0x03` (PCI_display) → `pciIo->GetLocation(..., &segment, &bus, &device, &function)`. Questa parte è pulita e isolabile dal resto (harvesting EDID via `efi_collect_display_edids()`, caricamento OpROM via `efi_find_code_type_image()`/`efi_try_load_oprom()`, capture ROM legacy via `efi_copy_legacy_rom()`, e le struct `boot_edid`/`legacy_vga_rom` in `kernel_args.h`) — tutto il resto va lasciato fuori per decisione già presa con l'utente.
- Precedente a 3 stadi per passare dati scoperti dal boot loader al kernel (da seguire per i nuovi dati): boot loader riempie un campo `kernel_args` → `frame_buffer_console_init_post_vm()` (`src/system/kernel/debug/frame_buffer_console.cpp` ~482-534) chiama `add_boot_item(COSTANTE, &dati, size)` → i driver chiamano `get_boot_item(COSTANTE, &size)`. Esempio reale: `VESA_EDID_BOOT_INFO "vesa_edid/v1"` (`headers/private/graphics/vesa/vesa_info.h:16`).

## Piano a fasi

### Fase A — ~~Boot loader: enumerazione PCI + rilevamento scheda primaria~~ → ordine GPU configurato dall'utente (IMPLEMENTATA, vedi sezione di stato in cima al documento)
Il design originale sotto è stato **scartato**: richiedeva `get_boot_item()` da app_server, che è kernel-only. L'alternativa scelta con l'utente, per evitare qualunque modifica al kernel, è interamente in app_server + preflet:
- `ScreenSettings::GpuOrder()`/`SetGpuOrder()`, `Desktop::_UpdateActiveGpuScreens()` applica l'ordine configurato, nuovo messaggio `AS_SET_GPU_ORDER`/`'gbdf'`, UI nella tab "Workspaces" (`BListView` + Move up/down). Vedi dettagli nella sezione di stato.

<details>
<summary>Design originale (scartato, mantenuto solo per riferimento storico)</summary>

- **EFI** (`src/system/boot/platform/efi/pci_video.cpp`, nuovo file): portare solo il loop enumerazione (`LocateHandleBuffer`+class-check+`GetLocation()`) da `MultiGPUActivation`, senza OpROM/EDID-harvesting.
- **BIOS legacy** (nuovo `src/system/boot/platform/bios_ia32/pci_config.cpp`): scansione PCI mechanism #1 da zero (porte I/O `0xCF8`/`0xCFC`), stesso filtro `class_base==0x03`. Nulla da riusare — va scritto ex novo.
- **Rilevamento primaria** (tecnica unificante per entrambi i path): leggere **tutte le 6 BAR** di ogni dispositivo enumerato (offset PCI 0x10-0x24 — **non solo BAR0**: su molte GPU il framebuffer è in BAR1/BAR2, mentre BAR0 è spesso MMIO), scartare le BAR I/O (bit0=1 del valore letto), gestire le BAR a 64 bit (bit di tipo 1-2 nel valore, due slot consecutivi da combinare in una base a 64 bit), e confrontare il range con l'indirizzo fisico del framebuffer di boot già attivo: `GraphicsOutputProtocol->Mode->FrameBufferBase` su EFI, `ModeInfoBlock->PhysBasePtr` (offset 0x28 della VBE mode info) su BIOS. Il dispositivo la cui BAR contiene quell'indirizzo è la primaria. Fattorizzare il matching (indipendente dalla piattaforma una volta letti i byte di config) in un header condiviso, es. `headers/private/graphics/common/boot_pci_scan.h`.
- **Trasporto verso il kernel**: nuova struct minimale in `headers/private/kernel/boot/kernel_args.h`, es. `boot_gpu_info { uint16 segment; uint8 bus, device, function; bool is_primary; }` + count/array in `kernel_args` — **non** riusare `boot_edid`/`legacy_vga_rom` di `MultiGPUActivation`, sono pensate per altro. Nuova costante boot-item (es. `GPU_BOOT_INFO "gpu_list/v1"`) in header condiviso sul modello di `vesa_info.h`. Pubblicare via `add_boot_item()` in `frame_buffer_console_init_post_vm()`, accanto a `FRAME_BUFFER_BOOT_INFO`/`VESA_EDID_BOOT_INFO`.

</details>

### Fase B — Caricamento driver kernel per le secondarie
**Nessuna modifica necessaria** (confermato sopra).

### Fase C — app_server: `ScreenManager` + `AccelerantHWInterface` multi-istanza
- `ScreenManager::_ScanDrivers()`: enumerare tutte le voci `/dev/graphics/*` (riusando la logica di scansione esistente, escludendo `vesa`/`framebuffer` come oggi) e costruire **un `AccelerantHWInterface` per ogni percorso distinto**, tramite un nuovo overload con path esplicito (mantenere la versione scan-and-break come fallback per `HAIKU_TARGET_PLATFORM_LIBBE_TEST`). Chiamare `_AddHWInterface()` per ciascuno.
- `AccelerantHWInterface.h`/`.cpp`: nuovo `Initialize(const char* devicePath)`; memorizzare il path come membro; esporre `GetPciLocation(uint8* bus, uint8* device, uint8* function)` costruito su `GetDriverPath()` (parsing dei 6 hex finali), fallback "posizione ignota" per driver non conformi (`virtio`).
- Identificazione della primaria: **non più via boot loader/kernel_args** (vedi Fase A rivista) — `Desktop::_UpdateActiveGpuScreens()` ordina `activeGpuList` secondo l'ordine configurato dall'utente (`ScreenSettings::GpuOrder()`, correlato via `GetPciLocation()`), con fallback all'ordine di scoperta/readdir se non configurato. Non fare mai affidamento implicito sull'ordine di `Screen::ID()` come "primaria" senza passare da questa logica.

### Fase D — app_server: `VirtualScreen` a "singolo schermo attivo" + mappatura riga→GPU + DPMS
Verificato che nessuna funzionalità di app_server richiede più di un `HWInterface` vivo contemporaneamente (workspace indipendenti/non sovrapposti; l'anteprima "tutti i workspace" in `WorkspacesView.cpp` disegna rettangoli schematici da `Window::Frame()`, non pixel reali di altri schermi). Serve quindi **ripuntare** l'unico schermo attivo ad ogni cambio workspace, non un vero multiplexing simultaneo.

- `VirtualScreen.h`/`.cpp`: nuovo `SetActiveScreen(Screen*)` che imposta esplicitamente `fHWInterface`/`fDrawingEngine` **e** `fFrame` (al frame del solo schermo attivo, non alla somma di `UpdateFrame()`).
- `Desktop.cpp`: in `_SetCurrentWorkspaceConfiguration()`, dopo la chiamata esistente a `fVirtualScreen.SetConfiguration(...)` (che altrimenti lascerebbe "attivo" uno schermo non deterministico, vedi sopra), calcolare `row = fCurrentWorkspace / fSettings->WorkspacesColumns()` (stessa convenzione già usata in `WorkspacesView.cpp`: `row = i / columns`, `column = i % columns`) e chiamare `fVirtualScreen.SetActiveScreen(activeGpuList[row % activeGpuList.Count()])`.
- `activeGpuList` come membro di `Desktop`: lista ordinata (primaria in testa) delle sole schede con EDID valido. Ricalcolata al boot (`Desktop::Init()`) e di nuovo ogni volta che `'sedi'` cambia l'EDID di uno schermo (aggiungere, nel gestore `'sedi'` di `AppServer.cpp`, una notifica a `Desktop` per ricalcolare lista + sweep DPMS — oggi non esiste un percorso diretto di notifica EDID-change verso `Desktop`, va aggiunto).
- Sweep DPMS: scheda **senza** EDID → sempre `SetDPMSMode(B_DPMS_OFF)` (chiamata diretta sulla sua `HWInterface`) + esclusa da `activeGpuList`. Scheda **con** EDID ma riga non attualmente visibile → resta `B_DPMS_ON` (comportamento da monitor reale: resta acceso, semplicemente non compositing in quell'istante — non va spenta solo perché "non è il suo turno").
- Con una sola GPU attiva, `activeGpuList.Count() == 1` e la formula degenera naturalmente nel comportamento di oggi — trattare esplicitamente questo come percorso di fallback sicuro ad ogni livello (ScreenManager/VirtualScreen/Desktop), così chi ha una sola scheda non vede alcuna differenza di comportamento anche se qualcosa nelle fasi A/C ha un bug.

### Fase E — Gate del compositor
`Desktop.cpp:572` e `ScreenWindow.cpp:601`: da "la singola `HWInterface()`" a "tutte le schede in `activeGpuList` hanno `HasSetCursorBitmap()` vero e cursore hardware abilitato". Se anche una sola scheda attiva non lo supporta, il compositor resta disabilitato globalmente.

### Fase F — Screen preflet: `BTabView` + protocollo indicizzato + nuovo formato impostazioni
- `ScreenWindow.cpp`/`.h`: riscrittura in `BTabView`. Tab "Workspaces": spinner righe/colonne esistenti + nuova anteprima grafica a griglia raggruppata per scheda (vedi esempi dell'utente: 1 scheda → 1 etichetta + griglia; N schede → N etichette allineate ai rispettivi blocchi di righe) + checkbox "Enable 2D compositor" (spostato qui — vedi discussione compositor sopra). Una tab per scheda rilevata: tutti i controlli attuali (luminosità, risoluzione/profondità/refresh, Dump/Load EDID, cursore hardware, cursore animato, 2D accel) + nuovo spinner risoluzione per-riga-posseduta. Scheda senza EDID → controlli disabilitati nella sua tab + DPMS off (Fase D).
- `AppServer.cpp`: aggiungere `"screen"` int32 a `AS_SET_HW_CUR_BITMAP_ENABLED`, `AS_SET_CURSOR_ANI_SETTINGS`, `AS_SET_2D_ACCELERATION_ENABLED`, `AS_SET_2D_DEBUG_ENABLED` e ai getter `'hsbm'`/`'h2da'`/`'gedc'`/`'sedc'`/`'hcai'`, sul modello di `'gedi'`/`'sedi'`.
- `MonitorView.cpp`/`.h`: oggi disegna un solo rettangolo-monitor — va **riscritta** (non estesa) per una griglia di monitor raggruppati per scheda con etichetta.
- `ScreenSettings.cpp`/`.h`: formato `Screen_data` da blob binario posizionale a `BMessage` flattened, chiavi per-scheda (via BDF o identificativo stabile persistito). Aggiornare in sincrono anche `Desktop.cpp:547-577` e `AccelerantHWInterface.cpp:378-417`.

## Fuori scope (promemoria)

Due schede con lo stesso modulo driver (es. due Matrox G450+Millennium I, o due S3 Trio/Virge) — richiederebbe gestire override `.settings` per-dispositivo invece che per-modulo (`edid_override.h` è oggi keyed by driver module name) e disambiguare istanze identiche nella UI. Lavoro futuro, esplicitamente rimandato.

## File critici

- `src/system/boot/platform/efi/pci_video.cpp` (nuovo), `src/system/boot/platform/bios_ia32/pci_config.cpp` (nuovo)
- `headers/private/kernel/boot/kernel_args.h`, `src/system/kernel/debug/frame_buffer_console.cpp`
- `src/servers/app/ScreenManager.cpp`, `src/servers/app/VirtualScreen.{h,cpp}`, `src/servers/app/Desktop.cpp`
- `src/servers/app/drawing/interface/local/AccelerantHWInterface.{h,cpp}`
- `src/servers/app/AppServer.cpp`
- `src/preferences/screen/ScreenWindow.{h,cpp}`, `MonitorView.{h,cpp}`, `ScreenSettings.{h,cpp}`

## Verifica

Codice di boot/kernel/driver: non testabile in modo significativo senza hardware reale multi-GPU (o QEMU con due device video emulati, per entrambi i path BIOS/OVMF). Strategia per fase:
- **Fase A**: log seriale/`dprintf` con la lista BDF enumerata e la primaria scelta, confrontato a occhio con cosa il firmware ha effettivamente mostrato come boot display.
- **Fase C**: prima regressione su macchina single-GPU (deve continuare a produrre esattamente 1 `AccelerantHWInterface`), poi su hardware dual-GPU reale, incrociando il BDF ricavato da `GetPciLocation()` con l'output di `listdev -d` sulla stessa macchina (fonte di verità indipendente, nessun nuovo codice kernel necessario per questo confronto).
- **Fase D**: su hardware con 2 GPU attive e griglia 2x2, passare in rassegna i 4 workspace e confermare (visivamente + log del puntatore `VirtualScreen::HWInterface()`) che le righe si alternano in modo consistente tra le due schede.
- **Fase E/F**: testabili nel normale ciclo build/esegui/clicca; il caso critico da esercitare apposta è 2 GPU attive di cui una senza cursore hardware (il compositor deve restare disabilitato globalmente).
- In ogni fase, build incrementale con `jam -q -j$(nproc) app_server` (o il target specifico) prima di qualsiasi test su hardware, come da workflow abituale di questo fork.
