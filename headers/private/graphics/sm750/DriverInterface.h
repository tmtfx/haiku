/*
	Copyright 1999, Be Incorporated.	All Rights Reserved.
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
//#include <boot_item.h>
//#include <vesa_info.h>
#include <edid.h>
#include "video_overlay.h"

#define DRIVER_PREFIX "sm750"

/* --- Benaphore per la sincronizzazione --- */
#ifdef __cplusplus
/* Se stiamo compilando un file .cpp (Accelerante), usiamo i metodi */
struct Benaphore {
    sem_id  sem;
    int32   count;

    status_t Init(const char* name) {
        count = 0;
        sem = create_sem(0, name);
        return sem < 0 ? sem : B_OK;
    }

    status_t Acquire() {
        if (atomic_add(&count, 1) > 0)
            return acquire_sem(sem);
        return B_OK;
    }

    status_t Release() {
        if (atomic_add(&count, -1) > 1)
            return release_sem(sem);
        return B_OK;
    }

    void Delete() {
        delete_sem(sem);
    }
};
#else
/* Se stiamo compilando un file .c (Driver Kernel), definiamo solo i dati */
typedef struct {
    sem_id  sem;
    int32   count;
} Benaphore;
#endif


#if defined(__cplusplus)
extern "C" {
#endif


/*
typedef struct {
	sem_id	sem;
	int32	ben;
} benaphore;

#define INIT_BEN(x)		x.sem = create_sem(0, "SM750 "#x" benaphore");	x.ben = 0;
#define AQUIRE_BEN(x)	if((atomic_add(&(x.ben), 1)) >= 1) acquire_sem(x.sem);
#define RELEASE_BEN(x)	if((atomic_add(&(x.ben), -1)) > 1) release_sem(x.sem);
#define DELETE_BEN(x)	delete_sem(x.sem);*/

/* Dualhead & Output Flags */
#define DUALHEAD_OFF		(0<<6)
#define DUALHEAD_CLONE	(1<<6)
#define DUALHEAD_ON		(2<<6)
#define DUALHEAD_SWITCH	(3<<6)
#define DUALHEAD_CAPABLE	(1<<8)

/* Bitmask per operazioni differite (Interrupt Handler) */
#define SKD_MOVE_CURSOR		0x00000001
#define SKD_PROGRAM_CLUT	 0x00000002
#define SKD_SET_START_ADDR	0x00000004
#define SKD_SET_CURSOR		0x00000008
#define SKD_HANDLER_INSTALLED 0x80000000

// bianco e nero
#define CRT_CURSOR_VRAM_OFFSET	(si->card_info.mem_size - 1024)
#define CURSOR_VRAM_SIZE		1024
//#define CURSOR_VRAM_SIZE (16 * 1024)
//#define CRT_CURSOR_VRAM_OFFSET (si->card_info.mem_size - CURSOR_VRAM_SIZE)

#define MAX_EDID_MODES 8 // Di solito 4 detailed + eventuali standard

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
	char	accelerant[B_FILE_NAME_LENGTH];
	bool	dumprom;
	uint32 logmask;
	uint32 memory;		/* Forza riconoscimento memoria */
	bool	usebios;
	bool	hardcursor;
	bool	dualhead;
	bool	force_pci;
} sm750_settings;


typedef struct ChipInfo {
	uint16		chipID;
	uint16		chipType;
	const char*	chipName;
} ChipInfo;


/* Informazioni condivise tra Driver e Accelerante */
typedef struct {
	/* Identificazione PCI */
	uint16	vendor_id;
	uint16	device_id;
	uint8	revision;
	uint8	bus;
	uint8	device;
	uint8	function;
	//uint8	edid_panel[128];	// Dati monitor su LCD/Panel
	//uint8	edid_crt[128]; // Dati monitor su VGA/CRT
	edid1_info vesa_edid_info;
	char	device_path[B_PATH_NAME_LENGTH];

	/* Mappature Memoria */
	area_id	regs_area;	/* BAR0: Registri MMIO */
	area_id	fb_area;		/* BAR1: Framebuffer */
	
	vuint32		*regs;		/* Puntatore virtuale ai registri (MMIO) */
	uint8		*framebuffer; /* Puntatore virtuale alla video RAM */
	phys_addr_t	framebuffer_pci; /* Indirizzo fisico (bus) per DMA */

	/* Gestore Memoria Video (Heap) */
	void	*mem_mgr;	 /* Puntatore al gestore memoria (importante!) */
	
	/* Modalità Schermo */
	area_id	mode_list_area;	/* Lista dei display_mode supportati da implementare in futuro*/
	uint32	mode_count;     /* Numero di modi nell'area */
	display_mode	*mode_list; /* Puntatore alla lista (valido nell'accelerante) */

	uint32	flags;
	uint32	bits_per_pixel; // TODO, remove if unused (used for initial tests)
	sem_id	vblank;		/* Semaforo sincronizzazione verticale */

	/* Cursore Hardware */
	struct {
		uint16	hot_x;
		uint16	hot_y;
		uint16	x;
		uint16	y;
		uint16	width;
		uint16	height;
		bool	is_visible;
		phys_addr_t	pci_address; 
		void	*v_address;
	} cursor;

	/* DAC Palette (CLUT) */
	uint8	color_data[3 * 256];

	/* Stato Schermi */
	display_mode dm;		/* Timing testa primaria (CRT/Panel) */
	display_mode dm2;	 /* Timing testa secondaria */
	display_mode preferred_mode;	/* <--- La modalità nativa/preferita del monitor 1 */
	display_mode preferred_mode2; /* <--- La modalità nativa/preferita del monitor 2 */
	frame_buffer_config fbc;
	frame_buffer_config fbc2;

	/* Acceleration Engine Status */
	struct {
		Benaphore lock;
		uint32	fifo_slots; /* Slot liberi stimati */ // TODO lo usiamo?
		uint32 count;
	} engine;

	/* SM750 Specific Card Info (Sostituisce PINS NVIDIA) */
	struct {
		uint32	chip_id;		/* 0x750 o varianti */
		bool 	is_panel;		// true se usiamo LCD (0x80200), false se CRT (0x80000)
		//bool	has_edid_panel;	// Trovato EDID su canale Panel
		//bool	has_edid_crt;		// Trovato EDID su canale CRT
		bool	has_vesa_edid_info;
		uint32	active_outputs;	// Bitmask: 1=Panel, 2=CRT, 3=Entrambi
		uint32	mem_size;		/* Totale memoria rilevata */
		uint32	mem_type;		/* DDR, SDRAM, ecc. */
		
		/* Clock / PLL Limits */
		float	f_ref;		 /* Cristallo di riferimento (solitamente 24MHz) */
		uint32 max_sclk;		/* Max System Clock */
		uint32 max_mclk;		/* Max Memory Clock */
		uint32 max_pclk;		/* Max Pixel Clock */
	} card_info; /* SM750 Info */

	/* Overlay (Scaler) */
	struct {
		overlay_buffer myBuffer[MAXBUFFERS];
		int_buf_info	myBufInfo[MAXBUFFERS];
		overlay_token	myToken;
		Benaphore		lock;
		bool			active;
	} overlay;

	bool accelerant_in_use;
	sm750_settings settings;
} shared_info;

typedef struct {
	int		 fd;				 /* File descriptor del driver /dev/graphics/... */
	shared_info *si;				/* Puntatore alla shared info clonata */
	area_id	 shared_info_area;	/* ID area shared info */
	uint32		*regs;				/* Puntatore ai registri MMIO clonati */
	area_id	 regs_area;			/* ID area registri */
	area_id	 fb_area;			/* ID area framebuffer clonato */
	uint8 *framebuffer; /* Puntatore locale */
	display_mode*	mode_list;		// cloned list of standard display modes
	area_id			mode_list_area;
	edid1_info		edid_panel_info;
	edid1_info		edid_crt_info;
	bool	has_edid_panel;	// Trovato EDID su canale Panel
	bool	has_edid_crt;		// Trovato EDID su canale CRT
	bool		is_clone;			/* Vero se è un clone */
	engine_token sm750_engine_token; /* 2D engine token */
} accelerant_info;

/* Stato globale del driver */
typedef struct {
	uint32			openCount;
	int32			flags;
	pci_info		pci;			/* Nota: si chiama 'pci' ora */
	const ChipInfo* pChipInfo;
	area_id		 shared_area;
	shared_info* si;
	area_id		 regs_area;
	vuint32* regs;			/* Deve essere vuint32* */
	area_id		 fb_area;
	uint8* framebuffer;
	char			name[B_OS_NAME_LENGTH];
} DeviceInfo;

void sm750_init_chip(DeviceInfo *di);

/* Strutture per IOCTL */
typedef struct {
	uint32	magic;
	uint32	offset;
	uint32	size;
	uint32	value;
} sm750_get_set_pci;

typedef struct {
	uint32	magic;
	/*
	 * magic è una firma. L'accelerante scrive 0x12345678 nel campo 
	 * magic e il driver, prima di rispondere, controlla se quel numero 
	 * è corretto. Serve a evitare che un programma sbagliato chiami 
	 * l'IOCTL del tuo driver mandandolo in crash.
	 */
	area_id shared_info_area;
} sm750_get_private_data;
/* per ora non usiamo, può tornare utile se vogliamo supportare 
 * contemporaneamente più schede
 *
typedef struct {
	uint32	magic; 
	char	*name;
} sm750_device_name; */

//extern status_t control_device(void *cookie, uint32 op, void *arg, size_t len);


//void sm750_get_clocks(vuint32 *regs, shared_info *si);
void sm750_init_chip(DeviceInfo *di);

#if defined(__cplusplus)
}
#endif

#endif
