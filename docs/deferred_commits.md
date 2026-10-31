# Sincronizzazione master -> Sync-rev11: stato commit 425-575

Tutti i 151 commit del blocco #425-575 sono stati processati ed esaminati uno per
uno. Nessuno è rimasto davvero "in sospeso": ogni caso di sovrapposizione col lavoro
a mano è stato verificato e risolto (reintegrato se sicuro, lasciato fuori solo dove
davvero necessario).

## Riepilogo

- **128** commit applicati direttamente in cherry-pick, nessuna sovrapposizione.
- **20** commit erano stati applicati senza conflitto Git ma toccavano file con lavoro
  a mano altrove — tutti rivisti uno per uno con l'utente e **reintegrati** dopo aver
  verificato l'assenza di sovrapposizione reale (NVMM, radeon_hd, stack USB,
  intel_extreme, PCI, UFSHCI, build manifest, AboutSystem, poorman, usb_disk).
- **3** commit di traduzione (.catkeys) non si applicavano in cherry-pick — risolti
  copiando i cataloghi direttamente da master dopo verifica chiave-per-chiave: 14
  componenti senza modifiche a mano copiati in blocco, `devices`/`desklink`/
  `aboutsystem` copiati dopo conferma che non avevano voci di traduzione esclusive.

## Lavoro tuo esplicitamente NON toccato (per scelta dell'utente)

Nel file `PhysicalMemoryAllocator.cpp`/`usb_raw.cpp`/`ehci.cpp`/`uhci.cpp` esistevano
tuoi fix (`USB-STAB-02/03/04`, `USB-ROB-03a/08`, `USB-STAB-06/07`) che erano stati
annullati dal tuo stesso commit "restore original usb stack" perché causavano
instabilità. `USB-STAB-02`/`03` sono stati di fatto sanati dalle versioni upstream
appena reintegrate; `USB-STAB-04` e il lavoro isocrono EHCI/UHCI restano
**volutamente non ripristinati** — l'utente ha confermato di non volerli recuperare.

## Nota a margine

Il commit `NVMMMMMMH` (ramo pensato per la virtualizzazione NVMM) conteneva anche un
fix slegato per il lifecycle di `usb_disk` (`usb_disk_removed`,
`usb_disk_free_device_and_luns`) - verificato: nessun commit successivo lo ha più
toccato o invalidato, resta valido e coerente, semplicemente finito nel commit
sbagliato per il nome.
