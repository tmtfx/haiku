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
#include <edid.h>
#include <video_overlay.h>

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
    status_t Lock() {
        status_t status;
        if (atomic_add(&count, 1) > 0) {
            do {
                status = acquire_sem(sem);
            } while (status == B_INTERRUPTED);
            return status;
        }
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

//#define MAX_EDID_MODES 10 // Di solito 4 detailed + eventuali standard

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
	bool	force_CRT;
    bool	force_Panel;
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
	edid1_raw vesa_edid_raw;
	char	device_path[B_PATH_NAME_LENGTH];

	/* Mappature Memoria */
	area_id	regs_area;	/* BAR0: Registri MMIO */
	area_id	fb_area;		/* BAR1: Framebuffer */
	
	uint8		*framebuffer; /* Puntatore virtuale alla video RAM */
	phys_addr_t	framebuffer_pci; /* Indirizzo fisico (bus) per DMA */

	/* Gestore Memoria Video (Heap) */
	void	*mem_mgr;	 /* Puntatore al gestore memoria (importante!) */
	
	/* Modalità Schermo */
	area_id	mode_list_area;	/* Lista dei display_mode supportati da implementare in futuro*/
	uint32	mode_count;     /* Numero di modi nell'area */
	//display_mode	*mode_list; /* Puntatore alla lista (valido nell'accelerante) */

	uint32	flags;
	uint32	bits_per_pixel; // TODO, remove if unused (used for initial tests)
	uint32	framebuffer_size; // VRAM size used for frame buffer, conveninet variable, same as card_info.max_desktop_mem
	uint32	real_framebuffer_size; // frame buffer size occupied by actual resolution
	//uint32	first_free_vram_offset;
	/* offset available for video layer or anything else through memory_manager
	 * visto che è sempre uguale a card_info.max_desktop_mem non ha senso tenerlo
	 */
	
	/* Cursore Hardware */
	struct {
		uint16	hot_x;
		uint16	hot_y;
		uint16	x;
		uint16	y;
		uint16	width;
		uint16	height;
		bool	is_visible;
		phys_addr_t	vram_offset; 
		//void	*v_address;
		uint32          block_id;
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
		bool	has_edid_vesa;
		uint32	active_outputs;	// Bitmask: 1=Panel, 2=CRT, 3=Entrambi
		uint32	mem_size;		/* Totale memoria rilevata */
		uint32	mem_type;		/* DDR, SDRAM, ecc. */
		uint32 max_desktop_mem; // Max memory for desktop resolution supported by sm750
		
		/* Clock / PLL Limits */
		float	f_ref;		 /* Cristallo di riferimento (solitamente 24MHz) */
		uint32 max_sclk;		/* Max System Clock */
		uint32 max_mclk;		/* Max Memory Clock */
		uint32 max_pclk;		/* Max Pixel Clock */
	} card_info; /* SM750 Info */

	/* Overlay (Scaler - Layer #2 SM750) */
	struct {
		Benaphore   lock;            /* Protegge l'accesso all'unico Layer Video */
		uintptr_t   overlay_token;       /* Identificativo dell'overlay attivo */
		// Usiamo MAXBUFFERS per il Double/Triple buffering video
		overlay_buffer myBuffer[MAXBUFFERS];
		uint32         myBufferBlockID[MAXBUFFERS];
		bool        active;          /* Layer #2 è acceso? */
	} overlay;

	bool accelerant_in_use;
	int32   overlay_in_use; // Flag per l'allocazione esclusiva dell'overlay
	// Sincronizzazione V-Sync
    sem_id  vblank_sem;      // Segnale dal Kernel all'Accelerante
    sem_id  vblank_sync_sem; // Segnale dall'Accelerante all'App (User Sync)
    uint32  vblank_count;   // Contatore incrementato a ogni interrupt
    
    // Interrupt Management
    int32   irq_enabled;    // Flag di stato
	sm750_settings settings;
} shared_info;

typedef struct {
	int		 fd;				 /* File descriptor del driver /dev/graphics/... */
	shared_info *si;				/* Puntatore alla shared info clonata */
	area_id	 shared_info_area;	/* ID area shared info */
	vuint32		*regs;				/* Puntatore ai registri MMIO clonati */
	area_id	 regs_area;			/* ID area registri */
	area_id	 fb_area;			/* ID area framebuffer clonato */
	uint8 *framebuffer; /* Puntatore locale */
	display_mode*	mode_list;		// cloned list of standard display modes
	area_id			mode_list_area;
	edid1_info		edid_panel_info;
	edid1_info		edid_crt_info;
	edid1_info		edid_vesa_info;
	bool	has_edid_panel;	// Trovato EDID su canale Panel
	bool	has_edid_crt;		// Trovato EDID su canale CRT
	bool		is_clone;			/* Vero se è un clone */
	engine_token sm750_engine_token; /* 2D engine token */
	// Stato locale Overlay
    overlay_token   current_ot;      /* Token dell'overlay se allocato */
    const overlay_buffer *current_ob; /* Buffer attualmente visualizzato */
    const overlay_buffer *next_buffer_to_show;
    bool            overlay_active;
    thread_id       vblank_thread;
    void	*cursor_virtual_address;
} accelerant_info;

/* Stato globale del driver */
typedef struct {
	uint32			openCount;
	int32			flags;
	pci_info		pci;			/* Nota: si chiama 'pci' ora */
	const ChipInfo*	pChipInfo;
	area_id		 	shared_area;
	shared_info*	si;
	area_id		 	regs_area;
	vuint32*		regs;			/* Deve essere vuint32* */
	area_id			fb_area;
	uint8*			framebuffer;
	bool			msi_enabled;
	uint8			msi_vector;
	char			name[B_OS_NAME_LENGTH];
} DeviceInfo;

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


void sm750_init_chip(DeviceInfo *di);

#if defined(__cplusplus)
}
#endif

#endif
