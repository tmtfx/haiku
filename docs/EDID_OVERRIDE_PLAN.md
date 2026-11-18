# Piano di Abilitazione B_SET_EDID_INFO e Ricreazione display_modes

## Obiettivo
Estendere l'uso di `B_SET_EDID_INFO` in modo che i driver grafici accelerant non solo memorizzino i dati `edid1_info` nella propria struttura (es. `shared_info`), ma deallochino e ricreino immediatamente l'area di memoria `mode_list_area` con la funzione `create_display_modes(...)`. In questo modo, l'app_server e la preflet Screen riceveranno istantaneamente l'elenco aggiornato delle risoluzioni supportate, i pixel clock corretti e i limiti del monitor associati all'EDID iniettato.

---

## Architettura e Flusso

1. **Screen preflet (`ScreenWindow` & `ScreenMode`)**:
   - `_LoadEDID()` carica la struttura binaria `edid1_info` e la invia con il messaggio `'sedi'` ad `app_server`.
   - Se `app_server` risponde `B_OK`, `ScreenMode::UpdateModeList()` svuota la vecchia lista e interroga di nuovo l'`app_server` (`BScreen::GetModeList()`).
   - `ScreenWindow` aggiorna i menu (`_CheckResolutionMenu()`, `_CheckRefreshMenu()`), le descrizioni monitor (`_UpdateMonitor()`) e la modalità attiva (`_UpdateActiveMode()`).

2. **app_server (`AccelerantHWInterface`)**:
   - In `AccelerantHWInterface::SetEDID()` viene acquisito `AutoWriteLocker _(this)`.
   - Viene invocato l'hook `fAccSetEDIDInfo(...)`.
   - Se l'hook ritorna `B_OK`, viene richiamato `_UpdateModeList()` che recupera il nuovo conteggio e le nuove modalità dal driver.

3. **Accelerant grafici**:
   - Ognuno implementa `B_SET_EDID_INFO`.
   - Riceve la `edid1_info`.
   - Aggiorna i campi EDID (`edidInfo`, `has_edid = true`, ecc.).
   - Se `mode_list_area >= 0`, chiama `delete_area(mode_list_area)`.
   - Ricrea le modalità invocando la rispettiva routine (`create_mode_list()` o `create_display_modes(...)`).

---

## Driver Coinvolti

1. **`vesa`**:
   - Dealloca `gInfo->mode_list_area` e richiama `create_mode_list()`.
2. **`framebuffer`**:
   - Dealloca e ricrea la modalità framebuffer base aggiornando le info EDID.
3. **`intel_extreme`**:
   - Implementa `intel_set_edid_info`.
   - Copia in `gInfo->edid_info`, imposta `gInfo->has_edid = true`.
   - Dealloca `gInfo->mode_list_area` e invoca `create_display_modes("intel extreme modes", &gInfo->edid_info, ...)`.
4. **`radeon_hd`**:
   - Implementa `radeon_set_edid_info`.
   - Salva in `gDisplay[crtcID]->edidData` e `gInfo->shared_info->edid_info`.
   - Dealloca `gInfo->mode_list_area` e invoca `create_mode_list()`.
5. **`intel_arc`**:
   - Implementa `intel_arc_set_edid_info`.
   - Salva in `gInfo->edid_info`, imposta `gInfo->has_edid = true`.
   - Dealloca `gInfo->mode_list_area` e invoca `create_mode_list()`.
6. **`sm750`**:
   - Implementa `sm750_set_edid_info`.
   - Aggiorna `edid_panel_info` o `edid_crt_info`.
   - Dealloca l'area modalità e rigenera le modalità con `create_display_modes`.
7. **`ati`**:
   - Implementa `SetEdidInfo`.
   - Salva in `si.edidInfo`, imposta `si.bHaveEDID = true`.
   - Dealloca `gInfo.modeListArea` e richiama `CreateModeList()`.
8. **`kyro`**:
   - Implementa `SetEdidInfo`.
   - Salva in `gInfo.sharedInfo->edidInfo`, imposta `gInfo.sharedInfo->hasEdid = true`.
   - Dealloca `gInfo.modeListArea` e richiama `CreateModeList()`.
9. **`s3`**:
   - Implementa `SetEdidInfo`.
   - Salva in `si.edidInfo`, imposta `si.bHaveEDID = true`.
   - Dealloca `gInfo.modeListArea` e richiama `CreateModeList()`.
10. **`trident`**:
   - Implementa `SetEdidInfo`.
   - Salva in `si.edidInfo`, imposta `si.bHaveEDID = true`.
   - Dealloca `gInfo.modeListArea` e richiama `CreateModeList()`.
11. **`xgi`**:
   - Implementa `xgi_set_edid_info`.
   - Salva in `gInfo.sharedInfo->edidInfo` (o `edid`), imposta il flag EDID.
   - Dealloca `gInfo.modeListArea` e rigenera con `create_display_modes`.
