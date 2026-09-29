# Analisi Criticità e Cose Mancanti per BT878 / ATI TV Wonder VE

Questo documento riassume lo stato del driver e del media add-on Haiku per il chip Brooktree Bt878 (con particolare riferimento alla scheda ATI TV Wonder VE) e traccia gli interventi necessari per una reale utilizzazione dell'hardware.

---

## 1. Problemi Bloccanti (Impediscono la corretta visualizzazione)

### A. Mancata gestione dell'interlacciamento nel programma RISC
Il segnale video analogico PAL (576 linee) ed NTSC (480 linee) è **interlacciato** (50 o 60 semiquadri al secondo):
* Il chip Bt878 riceve prima il **Semiquadro Pari** (Field Even: 288 linee per PAL) seguito dall'impulso di sincronismo verticale (VBI/VSYNC).
* Subito dopo riceve il **Semiquadro Dispari** (Field Odd: 288 linee per PAL).

Nel codice originale:
* Il motore RISC veniva programmato per attendere un singolo sincronismo e catturare tutte le linee (es. 576) in modo lineare e sequenziale.
* Questo causava la divisione a metà dell'immagine (la metà superiore con il semiquadro pari compresso verticalmente e la metà inferiore con il semiquadro dispari).
* **Soluzione**: Implementare il **line interleaving** nel microprogramma RISC:
  * Sincronizzazione con il campo Pari (`VRE` / Field Even) e scrittura sulle linee pari (`0, 2, 4, 6...` con passo `bytes_per_line * 2`).
  * Sincronizzazione con il campo Dispari (`VRO` / Field Odd) e scrittura sulle linee dispari (`1, 3, 5, 7...` con passo `bytes_per_line * 2`).
  * Generazione dell'IRQ al termine del secondo semiquadro (frame completo).
  * Per le risoluzioni ridotte a metà (es. 360x288 o 320x240), cattura del solo semiquadro desiderato senza interleaving.

---

### B. Assenza di Double Buffering (Forte Tearing)
* Il driver allocava originariamente un solo buffer DMA.
* Mentre la CPU in spazio utente eseguiva la `read()` (e `user_memcpy()`) per copiare gli ~830 KB del frame, il controller DMA continuava a scrivere nello stesso buffer, causando un vistoso effetto tearing orizzontale a 25/30 fps.
* **Soluzione**: Implementare il **Double Buffering (Ping-Pong)**:
  * Allocare due buffer DMA per i frame (Buffer 0 e Buffer 1).
  * Il programma RISC (o due rami RISC alternati) scrive alternativamente nel Buffer 0 e nel Buffer 1.
  * La `read()` legge dal buffer completato più recente mentre il DMA scrive nell'altro.

---

### C. Avvio incontrollato del DMA in `open()`
* Il driver avviava la cattura hardware all'interno di `bt878_open()`, sovraccaricando bus PCI e CPU anche solo per interrogazioni informative (come la scansione dei nodi da parte del Media Server).
* **Soluzione**: Rimuovere l'avvio della cattura da `open()` e limitarla esclusivamente al comando ioctl `BTV_START_CAPTURE`, richiamato dal media add-on quando il nodo entra in stato attivo.

---

## 2. Funzionalità Aggiuntive per un Utilizzo Completo

### A. Gestione Audio
* L'add-on attuale è un `BBufferProducer` per flussi solo video (`B_MEDIA_RAW_VIDEO`).
* Per acquisire anche l'audio proveniente da sorgenti composite, è necessario collegare l'uscita audio della scheda TV (Line-Out) all'ingresso Line-In della scheda audio del computer, gestito tramite il mixer audio di sistema di Haiku. In alternativa, si potrà valutare l'implementazione del nodo audio dedicato o della gestione del chip audio PCI Bt878 (se presente e cablato).

### B. Deinterlacciamento Software
* Per sorgenti a 50i / 60i con soggetti in rapido movimento, i semiquadri interlacciati mostrano artefatti a pettine ("combing"). Un filtro di deinterlacciamento (bob, weave, blend) può essere aggiunto nel consumer o nel processing path.

### C. Gestione Errori FIFO e Ripristino DMA
* In caso di congestione del bus PCI (`BT848_INT_OFLOW`, `BT848_INT_SCERR`, `BT848_INT_OCERR`), la routine di interrupt deve riarmare il programma RISC e resettare i contatori FIFO per prevenire il freeze dello stream video.
