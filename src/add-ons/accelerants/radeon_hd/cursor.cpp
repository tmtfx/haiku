#include "accelerant.h"
#include "accelerant_protos.h"
#include "accelerant.h"
#include "evergreen_reg.h"
#include <Accelerant.h>
#include <math.h>
#include <OS.h>
#include <string.h>

#undef TRACE_CURSOR
#define TRACE_CURSOR
#ifdef TRACE_CURSOR
	// debug_printf (OS.h) scrive direttamente su _kern_debug_output, senza
	// passare dal flag _rtDebugFlag che filtra _sPrintf: piu' affidabile
	// per essere certi che il log esca davvero.
#	define CTRACE(x...) debug_printf("radeon_hd cursor: " x)
#else
#	define CTRACE(x...) ;
#endif

// Helper macros to read/write registers provided by accelerant.h

// Tutto quello che segue (wait_cursor_update_idle, il flush HDP, il giro
// disable/lock/unlock/enable, i log CTRACE) e' necessario SOLO sul chip APU
// "Aruba" (Trinity/Richland, RADEON_ARUBA): e' l'unica scheda su cui il
// cursore hardware resta agganciato alla bitmap precedente. Su tutte le
// altre schede (es. Turks/HD6570) il codice originale funziona gia' bene
// da solo, quindi resta intatto - incluso il fatto che non produce nessun
// log - e le contromisure/i log scattano solo quando serve.
static inline bool
is_radeon_aruba(void)
{
    return gInfo->shared_info->chipsetID == RADEON_ARUBA;
}

// Il cursore hardware ha un solo "pending update" in coda: se ne lanciamo
// un altro (nuova LOCK) mentre il precedente non e' ancora stato applicato
// dall'hardware (es. il vblank non e' ancora passato), l'aggiornamento puo'
// andare perso o essere applicato in modo incoerente - e' questo che causa
// il comportamento "a volte funziona, a volte no". Aspettiamo che
// EVERGREEN_CURSOR_UPDATE_PENDING si azzeri prima di iniziarne uno nuovo.
// Ritorna il numero di iterazioni attese (0 = gia' libero, altrimenti quante
// snooze(100) sono servite), o -1 se e' scattato il timeout.
static int32
wait_cursor_update_idle(void)
{
    for (int32 i = 0; i < 1000; i++) {
        if ((Read32(OUT, EVERGREEN_CUR_UPDATE) & EVERGREEN_CURSOR_UPDATE_PENDING) == 0)
            return i;
        snooze(100);
    }
    CTRACE("%s: TIMEOUT attendendo EVERGREEN_CURSOR_UPDATE_PENDING\n", __func__);
    return -1;
}


// Animazione cursore hardware: stesso schema del driver kyro (vedi
// src/add-ons/accelerants/kyro/cursor.cpp), un thread dedicato ricicla le
// slot caricate via radeon_set_cursor_bitmap() riprogrammando solo il
// registro di indirizzo superficie - niente riletture/riscritture della
// bitmap, quindi nessun impatto sulla CPU oltre un write32 ogni tick.
struct cursor_animation_state {
	bool		running;
	bool		terminate;
	float		speedFactor;
	bigtime_t	frameInterval;
	bigtime_t	startDelay;
	uint32		uploadedFrameCount;
	uint32		nextUploadFrame;
	uint32		currentFrame;
	sem_id		semaphore;
	thread_id	thread;
};

static cursor_animation_state sCursorAnimation = {
	false,
	false,
	10.0f,
	100000,
	0,
	1,
	0,
	0,
	-1,
	-1
};


static inline uint8*
cursor_frame_vaddr(uint32 frameIndex)
{
	if (gInfo == NULL || gInfo->shared_info == NULL
		|| frameIndex >= gInfo->cursor_frame_count) {
		return NULL;
	}
	return gInfo->shared_info->frame_buffer + gInfo->cursor_frame_fb_offset[frameIndex];
}


// Riprogramma solo l'indirizzo superficie del cursore hardware sulla slot
// indicata, lasciando intatte dimensione/hotspot/control gia' impostate
// dall'ultima radeon_set_cursor_bitmap(): e' quanto basta per avanzare di
// un frame durante l'animazione. Riusa lo stesso schema LOCK/UNLOCK (e, sul
// chip Aruba, lo stesso giro wait/hide/show) gia' validato su hardware reale
// in radeon_set_cursor_bitmap, per non reintrodurre il bug del cursore
// "incollato" alla prima bitmap caricata.
static void
program_cursor_frame_address(uint32 frameIndex)
{
	if (frameIndex >= gInfo->cursor_frame_count)
		return;

	bool isAruba = is_radeon_aruba();
	bigtime_t arubaStart = 0;
	bigtime_t arubaLapsed = 0;

	if (isAruba) {
		wait_cursor_update_idle();
		radeon_show_cursor(false);
		arubaStart = system_time();
		arubaLapsed = system_time() - arubaStart;
		if (arubaLapsed < 12000)
			snooze(12000 - arubaLapsed);
	}

	Write32(OUT, EVERGREEN_CUR_UPDATE, EVERGREEN_CURSOR_UPDATE_LOCK);

	uint64 gpu_cursor_phys = (uint64)gInfo->cursor_frame_fb_offset[frameIndex];
	Write32(OUT, EVERGREEN_CUR_SURFACE_ADDRESS, (uint32)(gpu_cursor_phys & 0xFFFFFFFF));
#ifdef EVERGREEN_CUR_SURFACE_ADDRESS_HIGH
	Write32(OUT, EVERGREEN_CUR_SURFACE_ADDRESS_HIGH, (uint32)(gpu_cursor_phys >> 32));
#endif

	if (isAruba)
		Read32(OUT, EVERGREEN_CUR_CONTROL); // forza il completamento della write (posted)

	Write32(OUT, EVERGREEN_CUR_UPDATE, 0);

	if (isAruba) {
		arubaStart = system_time();
		wait_cursor_update_idle();
		radeon_show_cursor(true);
		arubaLapsed = system_time() - arubaStart;
		if (arubaLapsed < 15000)
			snooze(15000 - arubaLapsed);
	}
}


static status_t
cursor_animation_thread(void* cookie)
{
	(void)cookie;

	while (!sCursorAnimation.terminate) {
		bigtime_t interval = sCursorAnimation.frameInterval;
		if (sCursorAnimation.currentFrame == 0 && sCursorAnimation.startDelay > 0)
			interval += sCursorAnimation.startDelay;

		bigtime_t timeout = sCursorAnimation.running ? interval : 500000;
		status_t status = acquire_sem_etc(sCursorAnimation.semaphore, 1,
			B_RELATIVE_TIMEOUT | B_CAN_INTERRUPT, timeout);
		if (sCursorAnimation.terminate)
			break;

		if (!sCursorAnimation.running || sCursorAnimation.uploadedFrameCount < 2)
			continue;

		if (status != B_TIMED_OUT)
			continue;

		sCursorAnimation.currentFrame++;
		if (sCursorAnimation.currentFrame >= sCursorAnimation.uploadedFrameCount)
			sCursorAnimation.currentFrame = 0;

		program_cursor_frame_address(sCursorAnimation.currentFrame);
	}

	return B_OK;
}


static status_t
ensure_animation_thread(void)
{
	if (sCursorAnimation.thread >= B_OK)
		return B_OK;

	sCursorAnimation.semaphore = create_sem(0, "radeon_hd cursor ani");
	if (sCursorAnimation.semaphore < B_OK)
		return sCursorAnimation.semaphore;

	sCursorAnimation.terminate = false;
	sCursorAnimation.thread = spawn_thread(cursor_animation_thread,
		"radeon_hd cursor ani", B_DISPLAY_PRIORITY, NULL);
	if (sCursorAnimation.thread < B_OK) {
		delete_sem(sCursorAnimation.semaphore);
		sCursorAnimation.semaphore = -1;
		return sCursorAnimation.thread;
	}

	resume_thread(sCursorAnimation.thread);
	return B_OK;
}

uint32 radeon_get_cursor_bits(void)
{
	// Modern GPUs support true-color cursors with alpha
	return 32;
}
status_t radeon_set_cursor_bitmap(uint16 width, uint16 height, uint16 hotX, uint16 hotY,
    color_space colorSpace, uint16 bytesPerRow, const uint8* bitmapData)
{
    if (width > 64 || height > 64 || hotX >= width || hotY >= height)
        return B_ERROR;

    // Sicurezza: Verifichiamo l'allocazione globale eseguita in init_common
    if (gInfo->cursor_vaddr == NULL)
        return B_NO_INIT;

    // Selezione della slot di destinazione per l'animazione: stesso schema
    // del driver kyro. Finche' l'animazione non e' avviata, ogni chiamata
    // carica la slot successiva in sequenza (frame 0, 1, 2, ...); una volta
    // avviata (StartCursorAnimation), il caricamento rimpiazza la slot
    // correntemente visualizzata dal thread di animazione.
    if (ensure_animation_thread() != B_OK)
        return B_ERROR;

    if (!sCursorAnimation.running) {
        if (sCursorAnimation.nextUploadFrame == 0)
            sCursorAnimation.uploadedFrameCount = 0;
        sCursorAnimation.currentFrame = sCursorAnimation.nextUploadFrame;
    }

    uint32 targetFrame = sCursorAnimation.running
        ? sCursorAnimation.currentFrame : sCursorAnimation.nextUploadFrame;
    if (targetFrame >= gInfo->cursor_frame_count)
        targetFrame = 0;

    uint8* dest = cursor_frame_vaddr(targetFrame);
    if (dest == NULL)
        return B_NO_INIT;

    // Pulisci l'area del cursore
    memset(dest, 0, 64 * 64 * 4);

    if (colorSpace != B_RGBA32 && colorSpace != B_RGB32)
        return B_ERROR;

    // Copia e conversione in ARGB8888
    for (int y = 0; y < height && y < 64; y++) {
        const uint8* srcRow = bitmapData + y * bytesPerRow;
        uint32* dstRow = (uint32*)(dest + y * 64 * 4);
        for (int x = 0; x < width && x < 64; x++) {
            const uint8* pixel = srcRow + x * 4;
            uint8 b = pixel[0];
            uint8 g = pixel[1];
            uint8 r = pixel[2];
            uint8 a = (colorSpace == B_RGBA32) ? pixel[3] : 0xFF;

            if (a == 0)
                continue;

            uint32 packed = ((uint32)a << 24) | ((uint32)r << 16) | ((uint32)g << 8) | (uint32)b;
            dstRow[x] = packed;
        }
    }
	// PER TEST, prova a vedere se il quadrato effettivamente viene letto (colore bianco) e quindi la copia
	// dei byte è dalla sorgente sbagliata! (copia dal framebuffer)
	// memset(dest, 0xFF, 64 * 64 * 4);

    bool isAruba = is_radeon_aruba();
    bigtime_t arubaStart = 0;
    bigtime_t arubaLapsed = 0;

    if (isAruba) {
        //arubaStart = system_time();

        // Il buffer del cursore e' mappato write-combining: gli store della
        // CPU possono restare nei WC buffer prima di diventare visibili in
        // RAM. Svuotiamoli PRIMA di invalidare la cache HDP (sotto),
        // altrimenti invalidiamo una cache che punta comunque ai byte
        // vecchi, perche' quelli nuovi non sono ancora arrivati in memoria.
        __sync_synchronize();

        // L'HDP (Host Data Path) ha una propria cache di LETTURA lato GPU tra
        // i motori video (incluso il piano cursore) e la VRAM. Senza questo
        // flush, il cursore resta agganciato alla prima bitmap caricata.
        Write32(OUT, EVERGREEN_HDP_MEM_COHERENCY_FLUSH_CNTL, 0);
        //CTRACE("%s: SYNC + FLUSH completato in %" B_PRId64 " us\n", __func__, system_time() - arubaStart); <-- risultati tra 0 e 1 us
    }

    gInfo->cursor_width = width;
    gInfo->cursor_height = height;
    gInfo->cursor_hotx = hotX;
    gInfo->cursor_hoty = hotY;

    if (isAruba) {
    	arubaStart = system_time();
        //CTRACE("%s: before update, CUR_CONTROL=0x%08" B_PRIx32
        //    " vramStart=0x%08" B_PRIx64 " cursorOffset=0x%08" B_PRIx32
        //    " chipsetFlags=0x%08" B_PRIx32 "\n", __func__,
        //    Read32(OUT, EVERGREEN_CUR_CONTROL), gInfo->fb.vramStart,
        //    gInfo->cursor_fb_offset, gInfo->shared_info->chipsetFlags);

        // Aspettiamo che un eventuale update precedente sia stato recepito
        // dall'hardware prima di accodarne un altro.
        //int32 waitTicks = wait_cursor_update_idle();
        wait_cursor_update_idle();
        //if (waitTicks != 0) {
        //    CTRACE("%s: wait pre-update: %" B_PRId32 " tick(s) da 100us (0=subito libero, -1=timeout)\n",
        //        __func__, waitTicks);
        //}

        // Disabilitiamo il cursore PRIMA del LOCK, non dopo: se lo
        // scrivessimo a transazione gia' aperta, l'hardware potrebbe
        // accodare EN=0 insieme al resto e farlo scattare solo all'UNLOCK
        // insieme al valore finale (EN=1), senza mai generare un vero
        // fronte 0->1 visibile.
        radeon_show_cursor(false);
        //CTRACE("%s: WAIT + HIDE completato in %" B_PRId64 " us\n", __func__, system_time() - arubaStart); <- tra i 460 e i 15505 us
        arubaLapsed = system_time() - arubaStart;
        if (arubaLapsed < 12000){
        	snooze(12000-arubaLapsed);
        }
    }

    // LOCK UPDATE prima della scrittura dei registri geometrie
    Write32(OUT, EVERGREEN_CUR_UPDATE, EVERGREEN_CURSOR_UPDATE_LOCK);

	uint64 gpu_cursor_phys = (uint64)gInfo->cursor_frame_fb_offset[targetFrame];

    Write32(OUT, EVERGREEN_CUR_SURFACE_ADDRESS, (uint32)(gpu_cursor_phys & 0xFFFFFFFF));
#ifdef EVERGREEN_CUR_SURFACE_ADDRESS_HIGH
    Write32(OUT, EVERGREEN_CUR_SURFACE_ADDRESS_HIGH, (uint32)(gpu_cursor_phys >> 32));
#endif

	Write32(OUT, EVERGREEN_CUR_SIZE, ((uint32)gInfo->cursor_height << 16) | (uint32)gInfo->cursor_width);
    Write32(OUT, EVERGREEN_CUR_HOT_SPOT, ((uint32)gInfo->cursor_hoty << 16) | (uint32)gInfo->cursor_hotx);
	// uint32 control = EVERGREEN_CURSOR_EN | EVERGREEN_CURSOR_24_8_UNPRE_MULT | EVERGREEN_CURSOR_FORCE_MC_ON; //mi genera quadrato con colori invertiti dello sfondo
    uint32 control = EVERGREEN_CURSOR_EN
                   | EVERGREEN_CURSOR_MODE(2) // <- quadrato modalità 32bit colore
                   | EVERGREEN_CURSOR_FORCE_MC_ON;
    Write32(OUT, EVERGREEN_CUR_CONTROL, control);

    if (isAruba) {
        Read32(OUT, EVERGREEN_CUR_CONTROL);
            // forza il completamento della write (posted) prima di procedere
    }

    // UNLOCK UPDATE
    Write32(OUT, EVERGREEN_CUR_UPDATE, 0);

    if (isAruba) {
    	arubaStart = system_time();
        // Confermiamo che QUESTO update sia stato recepito dall'hardware
        // prima di ritornare, invece di fidarci che sia gia' avvenuto.
        //int32 waitTicksPost = wait_cursor_update_idle();
        wait_cursor_update_idle();
        //if (waitTicksPost != 0) {
        //    CTRACE("%s: wait post-update: %" B_PRId32 " tick(s) da 100us (0=subito libero, -1=timeout)\n",
        //        __func__, waitTicksPost);
        //}

        // Riabilitiamo esplicitamente tramite show_cursor: fronte 0->1
        // completo hide -> update -> show.
        radeon_show_cursor(true);

        //CTRACE("%s: after update, CUR_CONTROL=0x%08" B_PRIx32
        //    " surfaceAddr=0x%08" B_PRIx32 "\n", __func__, Read32(OUT, EVERGREEN_CUR_CONTROL),
        //    Read32(OUT, EVERGREEN_CUR_SURFACE_ADDRESS));

        //CTRACE("%s: WAIT + SHOW completato in %" B_PRId64 " us\n",
        //    __func__, system_time() - arubaStart);
        arubaLapsed = system_time() - arubaStart;
        if (arubaLapsed < 15000){
        	snooze(15000-arubaLapsed);
        }
    }

    if (!sCursorAnimation.running) {
        if (targetFrame + 1 < gInfo->cursor_frame_count)
            sCursorAnimation.nextUploadFrame = targetFrame + 1;
        else
            sCursorAnimation.nextUploadFrame = 0;

        if (sCursorAnimation.uploadedFrameCount < targetFrame + 1)
            sCursorAnimation.uploadedFrameCount = targetFrame + 1;
    }

    return B_OK;
}

status_t radeon_set_cursor_shape(uint16 width, uint16 height, uint16 hotX, uint16 hotY,
    uint8* andMask, uint8* xorMask)
{
    if (width > 64 || height > 64 || hotX >= width || hotY >= height)
        return B_ERROR;

    if (gInfo->cursor_vaddr == NULL)
        return B_NO_INIT;

    memset(gInfo->cursor_vaddr, 0, 64 * 64 * 4);

    int maskBytesPerRow = (width + 7) / 8;

    for (int y = 0; y < height && y < 64; y++) {
        uint32* dstRow = (uint32*)(gInfo->cursor_vaddr + y * 64 * 4);
        const uint8* andRow = andMask + y * maskBytesPerRow;
        const uint8* xorRow = xorMask + y * maskBytesPerRow;
        for (int x = 0; x < width && x < 64; x++) {
            int byteIdx = x / 8;
            int bit = 7 - (x % 8);
            bool andBit = ((andRow[byteIdx] >> bit) & 0x1) != 0;
            bool xorBit = ((xorRow[byteIdx] >> bit) & 0x1) != 0;

            if (andBit) {
                continue;
            }
            uint8 r = xorBit ? 0x00 : 0xFF;
            uint8 g = xorBit ? 0x00 : 0xFF;
            uint8 b = xorBit ? 0x00 : 0xFF;
            uint8 a = 0xFF;
            uint32 packed = ((uint32)a << 24) | ((uint32)r << 16) | ((uint32)g << 8) | (uint32)b;
            dstRow[x] = packed;
        }
    }

    bool isAruba = is_radeon_aruba();
    bigtime_t arubaStart = 0;
    bigtime_t arubaLapsed = 0;

    if (isAruba) {
        // Vedi radeon_set_cursor_bitmap: svuotiamo i WC buffer della CPU
        // prima di invalidare la cache di lettura HDP lato GPU.
        __sync_synchronize();
        Write32(OUT, EVERGREEN_HDP_MEM_COHERENCY_FLUSH_CNTL, 0);
    }

    gInfo->cursor_width = width;
    gInfo->cursor_height = height;
    gInfo->cursor_hotx = hotX;
    gInfo->cursor_hoty = hotY;

    if (isAruba) {
        arubaStart = system_time();

        // Aspettiamo che un eventuale update precedente sia stato recepito
        // dall'hardware prima di accodarne un altro.
        wait_cursor_update_idle();

        // Vedi radeon_set_cursor_bitmap: disabilitiamo PRIMA del LOCK, cosi'
        // la scrittura di EN=0 e' immediata e non viene accodata nella
        // stessa transazione bloccata del valore finale (EN=1).
        radeon_show_cursor(false);

        // L'hardware ha bisogno di un margine reale (non solo ordinamento
        // CPU) per "digerire" wait+hide: portiamo il tempo totale trascorso
        // a un pavimento fisso, misurato empiricamente su questo chip.
        arubaLapsed = system_time() - arubaStart;
        if (arubaLapsed < 12000) {
            snooze(12000 - arubaLapsed);
        }
    }

    // LOCK UPDATE prima della scrittura dei registri geometrie
    Write32(OUT, EVERGREEN_CUR_UPDATE, EVERGREEN_CURSOR_UPDATE_LOCK);

	uint64 gpu_cursor_phys = (uint64)gInfo->cursor_fb_offset;

    Write32(OUT, EVERGREEN_CUR_SURFACE_ADDRESS, (uint32)(gpu_cursor_phys & 0xFFFFFFFF));
#ifdef EVERGREEN_CUR_SURFACE_ADDRESS_HIGH
    Write32(OUT, EVERGREEN_CUR_SURFACE_ADDRESS_HIGH, (uint32)(gpu_cursor_phys >> 32));
#endif

	Write32(OUT, EVERGREEN_CUR_SIZE, ((uint32)gInfo->cursor_height << 16) | (uint32)gInfo->cursor_width);
    Write32(OUT, EVERGREEN_CUR_HOT_SPOT, ((uint32)gInfo->cursor_hoty << 16) | (uint32)gInfo->cursor_hotx);
	// uint32 control = EVERGREEN_CURSOR_EN | EVERGREEN_CURSOR_24_8_UNPRE_MULT | EVERGREEN_CURSOR_FORCE_MC_ON; //mi genera quadrato con colori invertiti dello sfondo
    uint32 control = EVERGREEN_CURSOR_EN
                   | EVERGREEN_CURSOR_MODE(2) // <- quadrato modalità 32bit colore
                   | EVERGREEN_CURSOR_FORCE_MC_ON;
    Write32(OUT, EVERGREEN_CUR_CONTROL, control);

    if (isAruba) {
        Read32(OUT, EVERGREEN_CUR_CONTROL);
            // forza il completamento della write (posted) prima di procedere
    }

    // UNLOCK UPDATE
    Write32(OUT, EVERGREEN_CUR_UPDATE, 0);

    if (isAruba) {
        arubaStart = system_time();

        // Confermiamo che QUESTO update sia stato recepito dall'hardware
        // prima di ritornare, invece di fidarci che sia gia' avvenuto.
        wait_cursor_update_idle();

        // Riabilitiamo esplicitamente tramite show_cursor: fronte 0->1
        // completo hide -> update -> show.
        radeon_show_cursor(true);

        // Vedi sopra: pavimento fisso sul tempo totale trascorso per
        // wait+show.
        arubaLapsed = system_time() - arubaStart;
        if (arubaLapsed < 15000) {
            snooze(15000 - arubaLapsed);
        }
    }

    return B_OK;
}

void radeon_move_cursor(uint16 x, uint16 y)
{
	// Position register: high 16 = x, low 16 = y (match older drivers)
	uint32 pos = ((uint32)x << 16) | (uint32)y;
	Write32(OUT, EVERGREEN_CUR_POSITION, pos);
}

void radeon_show_cursor(bool isVisible)
{
	uint32 control = Read32(OUT, EVERGREEN_CUR_CONTROL);
	if (isVisible)
		control |= EVERGREEN_CURSOR_EN;
	else
		control &= ~EVERGREEN_CURSOR_EN;
	Write32(OUT, EVERGREEN_CUR_CONTROL, control);
}


bool
radeon_supports_cursor_ani(void)
{
	return gInfo != NULL && gInfo->cursor_vaddr != NULL
		&& gInfo->cursor_frame_count >= 2;
}


uint32
radeon_cursor_supported_frames(void)
{
	if (!radeon_supports_cursor_ani())
		return 0;

	return gInfo->cursor_frame_count - 1;
}


status_t
radeon_start_cursor_ani(void)
{
	if (!radeon_supports_cursor_ani())
		return B_NOT_SUPPORTED;

	if (sCursorAnimation.uploadedFrameCount < 2)
		return B_BAD_VALUE;

	if (ensure_animation_thread() != B_OK)
		return B_ERROR;

	sCursorAnimation.currentFrame = 0;
	program_cursor_frame_address(0);

	sCursorAnimation.running = true;
	if (sCursorAnimation.semaphore >= B_OK)
		release_sem(sCursorAnimation.semaphore);
	return B_OK;
}


status_t
radeon_stop_cursor_ani(void)
{
	if (!radeon_supports_cursor_ani())
		return B_NOT_SUPPORTED;

	sCursorAnimation.running = false;
	sCursorAnimation.currentFrame = 0;
	sCursorAnimation.nextUploadFrame = 0;
	program_cursor_frame_address(0);
	return B_OK;
}


status_t
radeon_set_cursor_ani_speed(float speedFactor)
{
	if (!radeon_supports_cursor_ani())
		return B_NOT_SUPPORTED;

	if (!isfinite(speedFactor) || speedFactor <= 0.0f)
		return B_BAD_VALUE;

	sCursorAnimation.speedFactor = speedFactor;
	sCursorAnimation.frameInterval = (bigtime_t)(1000000.0f / speedFactor);
	if (sCursorAnimation.frameInterval < 5000)
		sCursorAnimation.frameInterval = 5000;
	return B_OK;
}


status_t
radeon_set_cursor_ani_start_delay(uint32 startDelayMs)
{
	if (!radeon_supports_cursor_ani())
		return B_NOT_SUPPORTED;

	sCursorAnimation.startDelay = (bigtime_t)startDelayMs * 1000;
	return B_OK;
}


void
radeon_cursor_ani_uninit(void)
{
	if (sCursorAnimation.thread >= B_OK) {
		sCursorAnimation.terminate = true;
		if (sCursorAnimation.semaphore >= B_OK)
			release_sem(sCursorAnimation.semaphore);
		status_t result;
		wait_for_thread(sCursorAnimation.thread, &result);
	}

	if (sCursorAnimation.semaphore >= B_OK)
		delete_sem(sCursorAnimation.semaphore);

	sCursorAnimation.running = false;
	sCursorAnimation.terminate = false;
	sCursorAnimation.speedFactor = 10.0f;
	sCursorAnimation.frameInterval = 100000;
	sCursorAnimation.startDelay = 0;
	sCursorAnimation.uploadedFrameCount = 1;
	sCursorAnimation.nextUploadFrame = 0;
	sCursorAnimation.currentFrame = 0;
	sCursorAnimation.semaphore = -1;
	sCursorAnimation.thread = -1;
}
