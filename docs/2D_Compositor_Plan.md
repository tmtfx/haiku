# Haiku 2D Compositor Implementation Plan (Hybrid Alpha Blending)

## Background & Motivation
L'architettura grafica originale di Haiku (app_server) è basata sul Direct Rendering (Immediate Mode). Quando una finestra viene spostata e ne scopre un'altra, l'app_server invalida la regione e chiede all'applicazione sottostante di ricalcolare e ridisegnare la propria interfaccia vettoriale tramite AGG. Questo processo è intensivo per la CPU.
Per sfruttare le accelerazioni 2D hardware aggiunte nel fork "Pirati Del Frico" e alleggerire il carico sulla CPU evitando al contempo artefatti grafici attorno agli angoli arrotondati e alle tab delle finestre, implementeremo un Compositor 2D con **Hybrid Alpha Blending**.

## Scope & Impact
- **app_server**: Modifiche profonde al sistema di rendering e invalidazione in `Desktop` e `Window`.
- **Screen Preflet**: Aggiunta di un'opzione per abilitare il compositor, vincolata ai requisiti di sistema (accelerazione 2D e >= 4GB di RAM).
- **Prestazioni**: Notevole diminuzione dell'utilizzo della CPU durante lo spostamento delle finestre. Blit 2D rapido (`memcpy`) su aree opache al 100% e blending CPU ultra-ottimizzato solo sui pixel perimetrali con alpha < 255 (meno di 5 microsecondi).
- **Qualità Visiva**: Nessun artefatto rettangolare o nero sulle linguette delle finestre (tab) o sugli angoli arrotondati.
- **Licenza**: Tutto il codice scritto e aggiunto è compatibile con la licenza MIT.

## Proposed Solution
Il Compositor 2D funzionerà in modalità **Hybrid Alpha Blending**:
1. Ogni `Window` (o `ServerWindow`) alloca un buffer offscreen (un `UtilityBitmap` a 32-bit `B_RGBA32`) in memoria di sistema.
2. I comandi grafici inviati dalle app e dal decorator vengono disegnati in questo buffer offscreen.
3. Quando il desktop ricompone una regione danneggiata (a causa di finestre spostate o aggiornate):
   - Le finestre visibili vengono processate back-to-front sul background oppure front-to-back con occlusion mask.
   - Per ogni linea di pixel:
     - Se i pixel sono opachi (alpha == 255), viene eseguita una copia diretta (fast-path).
     - Se un pixel presenta trasparenza (0 < alpha < 255), viene applicata la formula di alpha blending standard Haiku: `C_dst = (C_src * alpha + C_dst * (255 - alpha)) >> 8`.
     - Se il pixel è completamente trasparente (alpha == 0, es. fuori dal bordo arrotondato della tab), il pixel di destinazione non viene modificato.

## Implementation Details
1. **Screen Preflet**: Toggle per il 2D Compositor subordinato alla presenza di accelerazione 2D e >= 4GB di RAM.
2. **Window Backing Store**: Allocazione di `UtilityBitmap` a 32 bit con `B_RGBA32` per supportare il canale alpha sia per i controlli che per il decorator.
3. **Desktop Compositing Engine**: Ciclo di compositing ottimizzato con fast-path a riga intera per sezioni opache e fallback per pixel con blending.

## Verification
- Verifica che `Screen` preflet abiliti la casella solo se i requisiti sono soddisfatti.
- Controllo visivo che le linguette delle finestre e i bordi non abbiano artefatti neri o rettangolari.
- Verifica con ProcessController dell'abbattimento del consumo CPU nello spostamento delle finestre.
