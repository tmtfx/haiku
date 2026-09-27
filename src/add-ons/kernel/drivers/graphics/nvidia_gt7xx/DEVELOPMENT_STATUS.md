# NVIDIA GT7xx / GK208 native driver status

Questo documento tiene traccia dello stato del lavoro sul driver/accelerante
`nvidia_gt7xx` per **NVIDIA GeForce GT 730 GK208** (`PCI ID 10de:1287`), di
cosa e' stato implementato, di cosa manca, e dei riferimenti tecnici usati.

## Obiettivo

Realizzare per Haiku un driver grafico **nativo** per la famiglia `DISP022X`
(GK208), separato dal vecchio driver `nvidia`, con:

- riuso del **boot mode** e dell'**EDID di boot** dal bootloader/kernel;
- modesetting nativo lato accelerante;
- costruzione di un backend EVO **clean-room** per `cl927d` (core channel) e
  `cl927c` (base channel);
- niente dipendenza dal percorso VESA per il cambio risoluzione.

## Perche' esiste un driver separato

Il vecchio driver `src/add-ons/kernel/drivers/graphics/nvidia/` nel tree Haiku
supporta solo generazioni precedenti a NV50 e dichiara esplicitamente che
**GeForce 8xxx e successive non sono supportate**. Per questo motivo e' stato
creato un nuovo driver separato `nvidia_gt7xx`.

## Cosa e' stato fatto

### 1. Nuovo driver/accelerante separato

Sono state create le directory:

- `headers/private/graphics/nvidia_gt7xx/`
- `src/add-ons/kernel/drivers/graphics/nvidia_gt7xx/`
- `src/add-ons/accelerants/nvidia_gt7xx/`

e i relativi hook nei Jamfile generali:

- `src/add-ons/kernel/drivers/graphics/Jamfile`
- `src/add-ons/accelerants/Jamfile`

### 2. Matching della scheda target

Il driver si attiva solo sulla **GT 730 GK208**:

- vendor: `0x10de`
- device: `0x1287`

Il matching viene fatto confrontando il framebuffer di boot con i BAR PCI della
scheda, in modo da individuare il device effettivamente avviato dal BIOS/GOP.

### 3. Reuse dell'EDID di boot

Il driver legge l'EDID di boot da:

- `get_boot_item(VESA_EDID_BOOT_INFO, NULL)`

e lo copia in `shared_info`, evitando per ora la dipendenza da una
implementazione I2C/DDC runtime.

Questo viene usato per:

- distinguere **uscita analogica vs digitale**;
- ricavare le capability DPMS di base;
- creare la mode list iniziale lato accelerante.

### 4. Mapping MMIO e framebuffer

Il kernel add-on:

- individua il BAR MMIO;
- individua il BAR framebuffer;
- mappa l'area MMIO;
- riusa il framebuffer di boot come superficie iniziale;
- condivide tutto attraverso `shared_info`.

### 5. Shared interface privata

Il file:

- `headers/private/graphics/nvidia_gt7xx/DriverInterface.h`

contiene ora:

- stato display condiviso;
- stato output attivo;
- informazioni su BAR/MMIO/framebuffer;
- strutture per i canali EVO;
- packet format interno per submission dei metodi;
- metadata di pushbuf / notifier / instance.

### 6. Core/EVO software path

E' stato creato un percorso di submission interno:

- ioctl `NVIDIA_GT7XX_SUBMIT_EVO`
- struct `nvidia_gt7xx_evo_push`
- lista di metodi `method/value`

L'accelerante costruisce sequenze contenenti metodi del tipo:

- `HEAD_SET_PIXEL_CLOCK`
- `HEAD_SET_CONTROL`
- `HEAD_SET_RASTER_*`
- `HEAD_SET_OFFSET`
- `HEAD_SET_SIZE`
- `HEAD_SET_STORAGE`
- `HEAD_SET_PARAMS`
- `SOR_SET_CONTROL` o `DAC_SET_CONTROL/POLARITY`
- `UPDATE`

### 7. Stato shadow dei metodi EVO

Nel kernel add-on i metodi vengono decodificati e riversati in uno stato
shadow (`shared_info->evo`) da cui si ricostruiscono:

- `display_mode.current_mode`
- `pixel_clock`
- geometria raster
- timing di blank/sync
- tipo di output
- `bytes_per_row`
- formato colore base

Questo consente di avere un protocollo interno coerente fra accelerante e
kernel add-on anche prima del vero aggancio al motore hardware.

### 8. Canali EVO distinti: core/base

Sono stati introdotti due canali software distinti:

- `NVIDIA_GT7XX_EVO_CHANNEL_CORE`
- `NVIDIA_GT7XX_EVO_CHANNEL_BASE`

con class IDs:

- `0x927d` per il **core**
- `0x927c` per il **base**

### 9. Push buffer DMA e notifier

Per ogni canale vengono ora allocate aree dedicate e cloneable:

- push buffer
- notifier

Le aree sono bloccate in memoria fisica 32-bit (`B_32_BIT_FULL_LOCK`) e il
driver risolve gli indirizzi fisici con:

- `get_memory_map()`

Questi indirizzi vengono conservati come base per il futuro binding verso il
motore GPU reale.

### 10. Instance blob / RAMFC-like image

Per ciascun canale e' stata aggiunta anche una **instance area** separata,
anch'essa fisicamente risolta, usata per costruire un primo blob di tipo
RAMFC-like clean-room contenente:

- class ID
- channel ID
- indirizzo fisico pushbuf
- indirizzo fisico notifier
- indirizzo fisico instance
- size dei buffer
- offset previsti per DMA user aperture
- offset previsti per PRAMIN
- campi runtime `PUT/GET`

Questo e' il primo pezzo del backend di **channel materialization**.

## Cosa NON e' ancora fatto

Le parti seguenti **non sono ancora implementate**:

### A. Vero channel object hardware

Manca il codice che traduca il blob `disp_chan_v0 + memory bindings + instance`
in un **vero oggetto canale EVO hardware** per GK208.

In particolare manca:

- binding reale di `pushbuf` come indirizzo GPU consumabile dall'hardware;
- binding reale del notifier;
- programmazione completa di **RAMFC / PRAMIN / CTXDMA**;
- creazione/inizializzazione dei canali `cl927d` e `cl927c` in modo che la GPU
  li riconosca davvero.

### B. Apertura user/channel aperture reale

Al momento gli offset:

- `PRAMIN`
- `DMA_USER`

sono solo tracciati e conservati nello stato condiviso. Manca il collegamento
con la vera **user aperture** della display engine.

### C. Consumo reale dei packet da parte della GPU

I packet DMA EVO sono oggi:

- formattati correttamente lato software;
- inseriti in pushbuf;
- riflessi nello stato shadow e nell'instance blob;

ma **non ancora consumati realmente dalla GPU**.

### D. Modesetting nativo completo

Manca ancora l'aggancio finale che faccia veramente eseguire al motore display:

- cambio pixel clock / PLL
- programmazione head
- routing output DAC/SOR
- latch via `UPDATE`
- page flip / base channel reale

### E. DPMS reale

Al momento DPMS resta minimale; non c'e' ancora programmazione hardware vera.

## Cosa manca in pratica per il prossimo step

Il prossimo step concreto nel kernel add-on e':

1. prendere il blob instance RAMFC-like;
2. scriverlo nella struttura/area che il motore GK208 si aspetta davvero;
3. bindare `pushbuf/notifier` all'hardware;
4. esporre la user aperture del canale;
5. fare avanzare il vero `PUT`, attendendo il `GET` hardware;
6. verificare che il motore EVO consumi i metodi del core channel.

Solo dopo quel passo si potra' dire che il backend EVO e' effettivamente vivo
in hardware.

## Perche' non si deve andare "a tentativi ciechi"

Non si sta procedendo alla cieca: ci sono riferimenti aperti sufficienti per
guidare una traduzione clean-room.

Pero' i riferimenti sono distribuiti tra:

- documentazione class/method;
- documentazione architetturale PFIFO/PRAMIN;
- codice Nouveau/Linux;
- infrastruttura driver Haiku esistente;

e Haiku **non** ha gia' nel tree un equivalente di NVKM/RM/NVRM per la parte
NVIDIA moderna. Quindi il lavoro da fare non e' "indovinare", ma
**ricostruire in modo originale** un piccolo layer di object/channel management
partendo da quelle specifiche.

## Riferimenti e documenti usati

### Haiku tree

- Vecchio driver NVIDIA:
  - `src/add-ons/kernel/drivers/graphics/nvidia/README.html`
  - `src/add-ons/kernel/drivers/graphics/nvidia/UPDATE.html`
- Driver/acceleranti di riferimento per struttura e plumbing:
  - `src/add-ons/kernel/drivers/graphics/vesa/`
  - `src/add-ons/accelerants/vesa/`
  - `src/add-ons/kernel/drivers/graphics/framebuffer/`
  - `src/add-ons/accelerants/framebuffer/`
  - `src/add-ons/kernel/drivers/graphics/intel_arc/`
  - `src/add-ons/accelerants/intel_arc/`
- Header utili:
  - `headers/private/kernel/frame_buffer_console.h`
  - `headers/private/graphics/common/edid.h`
  - `headers/os/drivers/KernelExport.h`
  - `headers/os/drivers/PCI.h`

### NVIDIA Open GPU Documentation

- EVO / Display class overview:
  - https://download.nvidia.com/open-gpu-doc/Display-Class-Methods/2/README.txt
- DISP022X core channel (`cl927d.h`):
  - https://download.nvidia.com/open-gpu-doc/Display-Class-Methods/2/cl927d.h
- DISP022X base channel (`cl927c.h`):
  - https://download.nvidia.com/open-gpu-doc/Display-Class-Methods/2/cl927c.h
- Open GPU documentation repository:
  - https://github.com/NVIDIA/open-gpu-doc
- DeepWiki display system overview:
  - https://deepwiki.com/NVIDIA/open-gpu-doc/3.2-display-system
- DeepWiki display core interface:
  - https://deepwiki.com/NVIDIA/open-gpu-doc/3.2.3-display-core-interface

### Nouveau / Linux sources

- Display channel argument (`nvif_disp_chan_v0`, `pushbuf`):
  - https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/nouveau/include/nvif/if0014.h
- Display engine channel creation flow:
  - https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/nouveau/dispnv50/disp.c
- Core channel setup example:
  - https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/nouveau/dispnv50/core507d.c
- Base channel setup example:
  - https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/nouveau/dispnv50/base507c.c
- Head programming example:
  - https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/nouveau/dispnv50/head507d.c

### Envytools / hardware docs

- MMIO map:
  - https://envytools.readthedocs.io/en/latest/hw/mmio.html
- Tesla PFIFO engine:
  - https://envytools.readthedocs.io/en/latest/hw/fifo/g80-pfifo.html
- NV4:G80 VRAM / PRAMIN overview:
  - https://envytools.readthedocs.io/en/latest/hw/memory/nv4-vram.html
- Envytools main docs:
  - https://envytools.readthedocs.io/en/latest/

## File locali modificati nel nuovo driver

### Kernel add-on

- `src/add-ons/kernel/drivers/graphics/nvidia_gt7xx/driver.cpp`
- `src/add-ons/kernel/drivers/graphics/nvidia_gt7xx/device.cpp`
- `src/add-ons/kernel/drivers/graphics/nvidia_gt7xx/vesa.cpp`
- `src/add-ons/kernel/drivers/graphics/nvidia_gt7xx/vesa_private.h`
- `src/add-ons/kernel/drivers/graphics/nvidia_gt7xx/Jamfile`
- `src/add-ons/kernel/drivers/graphics/nvidia_gt7xx/nvidia_gt7xx.settings`

### Accelerante

- `src/add-ons/accelerants/nvidia_gt7xx/accelerant.cpp`
- `src/add-ons/accelerants/nvidia_gt7xx/accelerant.h`
- `src/add-ons/accelerants/nvidia_gt7xx/mode.cpp`
- `src/add-ons/accelerants/nvidia_gt7xx/dpms.cpp`
- `src/add-ons/accelerants/nvidia_gt7xx/hooks.cpp`
- `src/add-ons/accelerants/nvidia_gt7xx/accelerant_protos.h`
- `src/add-ons/accelerants/nvidia_gt7xx/Jamfile`

### Header privati

- `headers/private/graphics/nvidia_gt7xx/DriverInterface.h`

## Stato attuale riassunto

### Funzionante oggi

- match della scheda GK208
- boot mode e boot EDID
- mapping MMIO / framebuffer
- accelerante collegato al kernel add-on
- protocollo EVO software-side
- canali software `core/base`
- pushbuf / notifier / instance con indirizzi fisici risolti
- image RAMFC-like iniziale

### Non ancora funzionante in hardware

- creazione vera dei channel objects display
- bind reale RAMFC/PRAMIN/CTXDMA
- user aperture hardware
- consumo hardware dei packet
- modeset reale oltre il boot mode

