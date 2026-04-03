# 🚀 SM750 Driver Development Log

## 📝 Stato del Progetto
* **Obiettivo:** Porting driver video Silicon Motion SM750 per Haiku OS.
* **Architettura:** Kernel Driver + User-space Accelerant.
* **Data Ultimo Aggiornamento:** Aprile 2026.

---

## ✅ Task Completati

### 1. Fondamenta & Interfaccia
* [x] **DriverInterface.h:** Definita la struttura `shared_info` con `addr_t` e `phys_addr_t`.
* [x] **Kernel Skeleton:** Creato `driver.c` con gestione `init_driver` e `publish_devices`.
* [x] **Mappatura PCI:** Implementata mappatura BAR0 (Registri) e BAR1 (Framebuffer) in `open_device`.

### 2. Gestione Memoria & Ciclo di Vita
* [x] **Memory Manager:** Integrato `memory_manager.c` comune di Haiku.
* [x] **Heap Init:** Inizializzazione VRAM e allocazione area cursore hardware (16KB).
* [x] **Lifecycle:** Implementati `init_accelerant` e `uninit_accelerant` (User-space) e `free_device` (Kernel).

### 3. Inizializzazione Hardware (Coldstart)
* [x] **init.c:** Separata la logica di boot in file dedicato.
* [x] **Chip Wake-up:** Implementato sblocco registri e passaggio a **Power Mode 0**.
* [x] **Clock Detection:** Creata funzione `sm750_get_clocks` per il debug dei PLL (MCLK/SCLK).

---

## 🛠 Task In Corso (Current Focus)

* [ ] **Pixel Test (Raw FB Access):** Verifica dell'integrità del BAR1 scrivendo pattern di test direttamente tramite indirizzo virtuale.
* [ ] **PLL Math:** Implementazione della formula $f_{out} = (f_{in} \times M/N) / 2^D$ per il calcolo preciso dei MHz.

---

## 📅 Prossimi Passi (To-Do)

1.  **Mode Setting:** Prima funzione di calcolo timing (VGA standard 640x480).
2.  **2D Engine:** Implementazione `FILL_RECTANGLE` usando il Graphic Engine HW.
3.  **Interrupts:** Gestione VBlank per eliminare il tearing.

---

## ⚠️ Note Tecniche
> **Attenzione:** Durante il Coldstart, il reset del Graphic Engine (GE) deve essere breve (`snooze(500)`) per evitare di bloccare il bus PCI se ci sono comandi pendenti.