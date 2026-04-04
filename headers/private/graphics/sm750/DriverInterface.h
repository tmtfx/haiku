/*
	Copyright 1999, Be Incorporated.   All Rights Reserved.
	This file may be used under the terms of the Be Sample Code License.
	Copyright 2026, Fabio Tomat.

	Other authors:
	Mark Watson;
	Apsed;
	Rudolf Cornelissen 10/2002-9/2004.
*/

#ifndef DRIVERINTERFACE_H
#define DRIVERINTERFACE_H

#include <Accelerant.h>
#include <Drivers.h>
#include <PCI.h>
#include <OS.h>
#include "video_overlay.h"

#define DRIVER_PREFIX "sm750"

#if defined(__cplusplus)
extern "C" {
#endif

/* --- Benaphore per la sincronizzazione --- */
typedef struct {
    sem_id  sem;
    int32   ben;
} benaphore;

#define INIT_BEN(x)      x.sem = create_sem(0, "SM750 "#x" benaphore");  x.ben = 0;
#define AQUIRE_BEN(x)    if((atomic_add(&(x.ben), 1)) >= 1) acquire_sem(x.sem);
#define RELEASE_BEN(x)   if((atomic_add(&(x.ben), -1)) > 1) release_sem(x.sem);
#define DELETE_BEN(x)    delete_sem(x.sem);

#define SM750_PRIVATE_DATA_MAGIC    0x750A /* SM750 revision A */

/* Dualhead & Output Flags */
#define DUALHEAD_OFF      (0<<6)
#define DUALHEAD_CLONE    (1<<6)
#define DUALHEAD_ON       (2<<6)
#define DUALHEAD_SWITCH   (3<<6)
#define DUALHEAD_CAPABLE  (1<<8)

/* Bitmask per operazioni differite (Interrupt Handler) */
#define SKD_MOVE_CURSOR      0x00000001
#define SKD_PROGRAM_CLUT     0x00000002
#define SKD_SET_START_ADDR   0x00000004
#define SKD_SET_CURSOR       0x00000008
#define SKD_HANDLER_INSTALLED 0x80000000

enum {
    ENG_GET_PRIVATE_DATA = B_DEVICE_OP_CODES_END + 1,
    ENG_GET_PCI,
    ENG_SET_PCI,
    ENG_DEVICE_NAME,
    ENG_RUN_INTERRUPTS
};

/* Max overlay buffers per SM750 */
#define MAXBUFFERS 3

typedef struct {
    uint16 slopspace;
    uint32 size;
} int_buf_info;

/* Impostazioni caricate dal file di configurazione (sm750.settings) */
typedef struct {
    char   accelerant[B_FILE_NAME_LENGTH];
    bool   dumprom;
    uint32 logmask;
    uint32 memory;      /* Forza riconoscimento memoria */
    bool   usebios;
    bool   hardcursor;
    bool   dualhead;
    bool   force_pci;
} sm750_settings;

/* Informazioni condivise tra Driver e Accelerante */
typedef struct {
    /* Identificazione PCI */
    uint16  vendor_id;
    uint16  device_id;
    uint8   revision;
    uint8   bus;
    uint8   device;
    uint8   function;

    /* Mappature Memoria */
    area_id regs_area;    /* BAR0: Registri MMIO */
    area_id fb_area;      /* BAR1: Framebuffer */
    
    uint32  *regs;        /* Puntatore virtuale ai registri (MMIO) */
    uint8   *framebuffer; /* Puntatore virtuale alla video RAM */
    phys_addr_t    framebuffer_pci; /* Indirizzo fisico (bus) per DMA */

    /* Gestore Memoria Video (Heap) */
    void    *mem_mgr;     /* Puntatore al gestore memoria (importante!) */
    
    /* Modalità Schermo */
    area_id mode_area;    /* Lista dei display_mode supportati */
    uint32  mode_count;

    uint32  flags;
    sem_id  vblank;       /* Semaforo sincronizzazione verticale */

    /* Cursore Hardware */
    struct {
        uint16  hot_x;
        uint16  hot_y;
        uint16  x;
        uint16  y;
        uint16  width;
        uint16  height;
        bool    is_visible;
        phys_addr_t pci_address; 
        void    *v_address;
    } cursor;

    /* DAC Palette (CLUT) */
    uint8   color_data[3 * 256];

    /* Stato Schermi */
    display_mode dm;      /* Timing testa primaria (CRT/Panel) */
    display_mode dm2;     /* Timing testa secondaria */
    frame_buffer_config fbc;
    frame_buffer_config fbc2;

    /* Acceleration Engine Status */
    struct {
        benaphore lock;
        uint32    fifo_slots; /* Slot liberi stimati */
    } engine;

    /* SM750 Specific Card Info (Sostituisce PINS NVIDIA) */
    struct {
        uint32 chip_id;       /* 0x750 o varianti */
        bool   is_mobile;     /* Se stiamo guidando un pannello interno */
        uint32 mem_size;      /* Totale memoria rilevata */
        uint32 mem_type;      /* DDR, SDRAM, ecc. */
        
        /* Clock / PLL Limits */
        float  f_ref;         /* Cristallo di riferimento (solitamente 24MHz) */
        uint32 max_sclk;      /* Max System Clock */
        uint32 max_mclk;      /* Max Memory Clock */
        uint32 max_pclk;      /* Max Pixel Clock */
    } card_info; /* SM750 Info */

    /* Overlay (Scaler) */
    struct {
        overlay_buffer myBuffer[MAXBUFFERS];
        int_buf_info   myBufInfo[MAXBUFFERS];
        overlay_token  myToken;
        benaphore      lock;
        bool           active;
    } overlay;

    bool accelerant_in_use;
    sm750_settings settings;
} shared_info;

void sm750_init_chip(shared_info *si);

/* Strutture per IOCTL */
typedef struct {
    uint32  magic;
    uint32  offset;
    uint32  size;
    uint32  value;
} sm750_get_set_pci;

typedef struct {
    uint32  magic;
    area_id shared_info_area;
} sm750_get_private_data;

typedef struct {
    uint32  magic;
    char    *name;
} sm750_device_name;

#if defined(__cplusplus)
}
#endif

#endif
