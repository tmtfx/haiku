/*
 * Copyright 2006-2008, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 */


#include "accelerant_protos.h"
#include "accelerant.h"

#include <inttypes.h>
#include <string.h>

struct IntelCursorRegisters {
    uint32 control;
    uint32 base;
    uint32 position;
    uint32 size;
    uint32 palette;
    uint32 argb_mode_mask;
    uint32 color2_mode_mask;
    bool is_modern; // true per Gen9+
};

static IntelCursorRegisters sCursorRegs;
static bool sCursorRegistersInitialized = false;

static void
init_cursor_registers_layout(void)
{
    if (sCursorRegistersInitialized)
        return;

    uint32 gen = gInfo->shared_info->device_type.Generation();
    
    // Rilevamento Pipe attiva (usando le macro di Haiku protette)
    uint32 pipeA = read32(INTEL_DISPLAY_A_PIPE_CONTROL) & (1 << 31);
    uint32 pipeB = read32(INTEL_DISPLAY_B_PIPE_CONTROL) & (1 << 31);
    bool isPipeB = (pipeB && !pipeA);

    // 1. LOG CRITICO: Verifichiamo lo stato delle Pipe e cosa risponde l'hardware
    debug_printf("Intel Extreme Cursor Debug: PipeA Control = 0x%08" B_PRIx32 ", PipeB Control = 0x%08" B_PRIx32 "\n", pipeA, pipeB);
    debug_printf("Intel Extreme Cursor Debug: Active Pipe is %s\n", isPipeB ? "PIPE_B" : "PIPE_A");
    
    // Reset della struttura
    memset(&sCursorRegs, 0, sizeof(IntelCursorRegisters));

    if (gen >= 9) {
        // --- ERA GEN9+ (Geminilake, Skylake, ecc.) ---
        uint32 pipeOffset = isPipeB ? 0x2000 : 0x0000;
        
        // Su Gen9 i registri base sono traslati di +0x40 rispetto a Gen4
        sCursorRegs.control  = (INTEL_CURSOR_CONTROL & 0xFF000000) | (0x700C0 + pipeOffset);
        sCursorRegs.base     = (INTEL_CURSOR_BASE    & 0xFF000000) | (0x700C4 + pipeOffset);
        sCursorRegs.position = (INTEL_CURSOR_POSITION & 0xFF000000) | (0x700C8 + pipeOffset);
        sCursorRegs.size     = (INTEL_CURSOR_SIZE     & 0xFF000000) | (0x700CC + pipeOffset);
        
        //sCursorRegs.argb_mode_mask   = 0x27; // MCURSOR_MODE_64_ARGB
        //sCursorRegs.color2_mode_mask = 0x06; // MCURSOR_MODE_64_2COLOR
        //sCursorRegs.is_modern        = true;
        sCursorRegs.argb_mode_mask   = (1 << 31) | 0x27; // MCURSOR_MODE_64_ARGB + ENABLE
		sCursorRegs.color2_mode_mask = (1 << 31) | 0x06; // MCURSOR_MODE_64_2COLOR + ENABLE
		sCursorRegs.is_modern        = true;
        debug_printf("Intel Extreme Cursor Debug: Gen9+ MMIO Layout -> Ctrl: 0x%08" B_PRIx32 ", Base: 0x%08" B_PRIx32 ", Pos: 0x%08" B_PRIx32 "\n",
            sCursorRegs.control, sCursorRegs.base, sCursorRegs.position);

    } else {
        // --- ERA PRE-GEN9 (Ironlake, Core2, ecc.) ---
        uint32 pipeOffset = (isPipeB && gen >= 5) ? 0x1000 : 0x0000;
        uint32 regBlock   = INTEL_CURSOR_CONTROL & 0xFF000000;

        sCursorRegs.control  = regBlock | (0x70080 + pipeOffset);
        sCursorRegs.base     = regBlock | (0x70084 + pipeOffset);
        sCursorRegs.position = regBlock | (0x70088 + pipeOffset);
        sCursorRegs.size     = regBlock | (0x700A0 + pipeOffset);
        sCursorRegs.palette  = regBlock | (0x700A4 + pipeOffset);
        sCursorRegs.is_modern        = false;

        if (gen >= 5) {
            sCursorRegs.argb_mode_mask   = 0x27; // Bit bassi per Gen5+
            sCursorRegs.color2_mode_mask = 0x06;
        } else {
            sCursorRegs.argb_mode_mask   = CURSOR_ENABLED | CURSOR_FORMAT_ARGB; // Vecchio stile Gen4 (bit 31)
            sCursorRegs.color2_mode_mask = CURSOR_ENABLED | CURSOR_FORMAT_2_COLORS;
        }
    }

    sCursorRegistersInitialized = true;
}

//#define ILK_CURSOR_MODE_DISABLE     0x00
//#define ILK_CURSOR_MODE_64_ARGB     0x27
//#define ILK_CURSOR_MODE_64_2COLOR   0x06

/*
static uint32
get_cursor_pipe_offset(void)
{
    // Verifica quale pipe è attiva leggendo i registri di controllo della Pipe
    uint32 pipeA = read32(INTEL_DISPLAY_A_PIPE_CONTROL) & (1 << 31);
    uint32 pipeB = read32(INTEL_DISPLAY_B_PIPE_CONTROL) & (1 << 31);

    if (pipeB && !pipeA)
        return 0x1000;    // Spostamento completo sulla Pipe B (comune su pannelli laptop)

    return 0;    // Pipe A standard
}
*/
/* questa è bicolore ma noi proviamo a convertirlo in argb
 * visto che intel lo digerisce meglio

status_t
intel_set_cursor_shape(uint16 width, uint16 height, uint16 hotX, uint16 hotY,
	uint8* andMask, uint8* xorMask)
{
	if (width > 64 || height > 64)
		return B_BAD_VALUE;

	init_cursor_registers_layout();

	// Spegne il cursore prima di toccare i buffer
	write32(sCursorRegs.control, 0);

	uint8* data = gInfo->shared_info->cursor_memory;
	uint8 byteWidth = (width + 7) / 8;

	// Copia del mascheramento binario (64-bit per linea layout Intel)
	for (int32 y = 0; y < height; y++) {
		for (int32 x = 0; x < byteWidth; x++) {
			data[16 * y + x] = andMask[byteWidth * y + x];
			data[16 * y + x + 8] = xorMask[byteWidth * y + x];
		}
	}

	// Imposta la palette Bianco/Nero (Solo Pre-Gen9, Gen9+ usa i piani universali)
	if (!sCursorRegs.is_modern && sCursorRegs.palette != 0) {
		write32(sCursorRegs.palette + 0, 0x00ffffff);
		write32(sCursorRegs.palette + 4, 0);
	}

	gInfo->shared_info->cursor_format = sCursorRegs.color2_mode_mask;

	// Attiva il cursore e imposta la dimensione
	write32(sCursorRegs.control, gInfo->shared_info->cursor_format);
	write32(sCursorRegs.size, (height << 12) | width);

	// Invia l'indirizzo base della memoria grafica
	write32(sCursorRegs.base, (uint32)gInfo->shared_info->physical_graphics_memory
		+ gInfo->shared_info->cursor_buffer_offset);

	// Gestione dell'Hot Spot e riposizionamento dinamico
	if (hotX != gInfo->shared_info->cursor_hot_x
		|| hotY != gInfo->shared_info->cursor_hot_y) {
		int32 x = read32(sCursorRegs.position);
		int32 y = x >> 16;
		x &= 0xffff;
		
		if (x & CURSOR_POSITION_NEGATIVE)
			x = -(x & CURSOR_POSITION_MASK);
		if (y & CURSOR_POSITION_NEGATIVE)
			y = -(y & CURSOR_POSITION_MASK);

		x += gInfo->shared_info->cursor_hot_x;
		y += gInfo->shared_info->cursor_hot_y;

		gInfo->shared_info->cursor_hot_x = hotX;
		gInfo->shared_info->cursor_hot_y = hotY;

		intel_move_cursor(x, y);
	}

	return B_OK;
}*/
status_t
intel_set_cursor_shape(uint16 width, uint16 height, uint16 hotX, uint16 hotY,
	uint8* andMask, uint8* xorMask)
{
	if (width > 64 || height > 64)
		return B_BAD_VALUE;

	init_cursor_registers_layout();

	// -------------------------------------------------------------------------
	// RAMO 1: Architetture Moderne (Gen 9 / Gen 9.5 - E.g., Celeron N4120)
	// -------------------------------------------------------------------------
	if (gInfo->shared_info->device_type.Generation() >= 9) {
		// 1. Disabilita temporaneamente il piano per resettare lo stato hardware
		write32(sCursorRegs.control, 0);

		uint32* dest = (uint32*)gInfo->shared_info->cursor_memory;
		
		// Pulisce il buffer hardware 64x64 per evitare artefatti ("fantasmi")
		memset(dest, 0, 64 * 64 * sizeof(uint32));

		// Convertiamo i bit bicolore AND/XOR in pixel ARGB a 32-bit lineari
		uint8 byteWidth = (width + 7) / 8;
		for (int32 y = 0; y < height; y++) {
			for (int32 x = 0; x < width; x++) {
				int32 byteIndex = byteWidth * y + (x / 8);
				int32 bitIndex = 7 - (x % 8);

				bool andBit = (andMask[byteIndex] >> bitIndex) & 1;
				bool xorBit = (xorMask[byteIndex] >> bitIndex) & 1;

				uint32 pixel = 0x00000000; // Trasparente (Sfondo)
				if (!andBit && !xorBit) {
					pixel = 0xFF000000; // Nero opaco
				} else if (!andBit && xorBit) {
					pixel = 0xFFFFFFFF; // Bianco opaco
				} else if (andBit && xorBit) {
					pixel = 0xFF000000; // Inversione (Approssimata a Nero su Gen9)
				}
				dest[64 * y + x] = pixel;
			}
		}

		// Salviamo il formato globale usando la maschera con bit 31 attivo
		gInfo->shared_info->cursor_format = sCursorRegs.argb_mode_mask;

		// 2. Forza la dimensione fissa 64x64 richiesta dall'hardware per l'ARGB
		write32(sCursorRegs.size, (64 << 12) | 64);
		
		// Imposta la posizione iniziale di partenza
		write32(sCursorRegs.position, 0);

		// 3. Configura le opzioni di controllo (Formato + Bit 31 di accensione)
		write32(sCursorRegs.control, gInfo->shared_info->cursor_format);
		
		// 4. Innesca l'hardware scrivendo l'indirizzo base (LATCH / Double Buffering)
		write32(sCursorRegs.base, (uint32)gInfo->shared_info->physical_graphics_memory
			+ gInfo->shared_info->cursor_buffer_offset);

		// Aggiorna lo stato dell'hotspot interno di Haiku
		gInfo->shared_info->cursor_hot_x = hotX;
		gInfo->shared_info->cursor_hot_y = hotY;

		return B_OK;
	}

	// -------------------------------------------------------------------------
	// RAMO 2: Architetture Legacy / Pre-Gen9 (Gen 4 / Gen 5 del tuo amico)
	// -------------------------------------------------------------------------
	
	// Disabilita il cursore sulla vecchia Pipe prima di manipolare i dati
	write32(sCursorRegs.control, 0);

	uint8* data = gInfo->shared_info->cursor_memory;
	uint8 byteWidth = (width + 7) / 8;

	// Copia vecchio stile nel buffer a due piani (AND + XOR)
	for (int32 y = 0; y < height; y++) {
		for (int32 x = 0; x < byteWidth; x++) {
			data[16 * y + x] = andMask[byteWidth * y + x];
			data[16 * y + x + 8] = xorMask[byteWidth * y + x];
		}
	}

	// Imposta la tavolozza hardware Bianco/Nero (Valido solo fino a Gen8)
	if (sCursorRegs.palette != 0) {
		write32(sCursorRegs.palette + 0, 0x00ffffff);
		write32(sCursorRegs.palette + 4, 0);
	}

	// Applica il formato calcolato per le vecchie generazioni (0x06 o vecchio bitmask)
	gInfo->shared_info->cursor_format = sCursorRegs.color2_mode_mask;

	write32(sCursorRegs.control, gInfo->shared_info->cursor_format);
	write32(sCursorRegs.size, (height << 12) | width);

	write32(sCursorRegs.base, (uint32)gInfo->shared_info->physical_graphics_memory
		+ gInfo->shared_info->cursor_buffer_offset);

	// Gestione dell'Hot Spot e riposizionamento dinamico per il vecchio codice
	if (hotX != gInfo->shared_info->cursor_hot_x
		|| hotY != gInfo->shared_info->cursor_hot_y) {
		int32 x = read32(sCursorRegs.position);
		int32 y = x >> 16;
		x &= 0xffff;
		
		if (x & CURSOR_POSITION_NEGATIVE)
			x = -(x & CURSOR_POSITION_MASK);
		if (y & CURSOR_POSITION_NEGATIVE)
			y = -(y & CURSOR_POSITION_MASK);

		x += gInfo->shared_info->cursor_hot_x;
		y += gInfo->shared_info->cursor_hot_y;

		gInfo->shared_info->cursor_hot_x = hotX;
		gInfo->shared_info->cursor_hot_y = hotY;

		intel_move_cursor(x, y);
	}

	return B_OK;
}

uint32
intel_get_cursor_bits(void)
{
	if (gInfo->shared_info->cursor_memory == NULL){
		debug_printf("Intel Extreme Accelerant: No cursor memory, assigning 0 bits\n");
		return 0;
	}

	if (gInfo->shared_info->device_type.Generation() >= 4) {
		debug_printf("Intel Extreme Accelerant: Cursor Color Depth 32-bit\n");
		return 32;
	}

	debug_printf("Intel Extreme Accelerant: Cursor Color Depth 1-bit\n");
	return 1;
}

status_t
intel_set_cursor_bitmap(uint16 width, uint16 height, uint16 hotX, uint16 hotY,
	color_space colorSpace, uint16 bytesPerRow, const uint8* bitmapData)
{
	if (width == 0 || height == 0 || width > 64 || height > 64)
		return B_BAD_VALUE;

	if (colorSpace != B_RGBA32 && colorSpace != B_RGB32)
		return B_BAD_TYPE;

	if (bytesPerRow < width * 4)
		return B_BAD_VALUE;

	if (gInfo->shared_info->cursor_memory == NULL || bitmapData == NULL)
		return B_NO_INIT;

	init_cursor_registers_layout();

	// -------------------------------------------------------------------------
	// RAMO 1: Architetture Moderne (Gen 9 / Gen 9.5 - E.g., Celeron N4120)
	// -------------------------------------------------------------------------
	if (gInfo->shared_info->device_type.Generation() >= 9) {
		debug_printf("Intel Cursor Trace: --- START intel_set_cursor_bitmap (Gen9+) ---\n");

		// 1. Disabilita temporaneamente il piano per resettare lo stato hardware
		debug_printf("Intel Cursor Trace: Disabling plane. Writing 0 to Reg 0x%08" B_PRIx32 "\n", sCursorRegs.control);
		write32(sCursorRegs.control, 0);
		debug_printf("Intel Cursor Trace: Reg 0x%08" B_PRIx32 " readback after disable = 0x%08" B_PRIx32 "\n", 
			sCursorRegs.control, read32(sCursorRegs.control));

		uint32* dest = (uint32*)gInfo->shared_info->cursor_memory;
		const uint32* src = (const uint32*)bitmapData;
		uint32 srcPixelsPerRow = bytesPerRow / 4;

		debug_printf("Intel Cursor Trace: Copying bitmap (%dx%d), SrcStride: %" B_PRIu32 " px, DestMem: %p\n", 
			width, height, srcPixelsPerRow, dest);

		// Pulisce l'intero buffer hardware 64x64 per evitare artefatti grafici
		memset(dest, 0, 64 * 64 * sizeof(uint32));

		// Copia i pixel dentro il layout hardware a 64 pixel di larghezza fissa
		int32 copiedPixels = 0;
		for (int32 y = 0; y < height; y++) {
			for (int32 x = 0; x < width; x++) {
				uint32 pixel = src[srcPixelsPerRow * y + x];
				if (colorSpace == B_RGB32)
					pixel |= 0xff000000; // Forza opaco se manca l'alpha channel
				dest[64 * y + x] = pixel;
				copiedPixels++;
			}
		}
		debug_printf("Intel Cursor Trace: Copied %d pixels into cursor shared memory\n", copiedPixels);

		// Salviamo il formato globale usando la maschera con bit 31 attivo
		gInfo->shared_info->cursor_format = sCursorRegs.argb_mode_mask;
		debug_printf("Intel Cursor Trace: Set shared_info->cursor_format = 0x%08" B_PRIx32 "\n", gInfo->shared_info->cursor_format);

		// 2. Forza la dimension fissa 64x64 richiesta dall'hardware per l'ARGB
		uint32 sizeValue = (64 << 12) | 64;
		debug_printf("Intel Cursor Trace: Writing Size (64x64) 0x%08" B_PRIx32 " to Reg 0x%08" B_PRIx32 "\n", sizeValue, sCursorRegs.size);
		write32(sCursorRegs.size, sizeValue);

		// Leggi e ripristina la posizione corrente
		int32 currentPos = read32(sCursorRegs.position);
		debug_printf("Intel Cursor Trace: Read current position raw value = 0x%08" B_PRIx32 " from Reg 0x%08" B_PRIx32 "\n", currentPos, sCursorRegs.position);
		
		debug_printf("Intel Cursor Trace: Writing current position back to Reg 0x%08" B_PRIx32 "\n", sCursorRegs.position);
		write32(sCursorRegs.position, currentPos);

		// 3. Configura le opzioni di controllo (Formato + Bit 31 di accensione)
		debug_printf("Intel Cursor Trace: Writing Control (Enable+Format) 0x%08" B_PRIx32 " to Reg 0x%08" B_PRIx32 "\n", gInfo->shared_info->cursor_format, sCursorRegs.control);
		write32(sCursorRegs.control, gInfo->shared_info->cursor_format);
		debug_printf("Intel Cursor Trace: Reg 0x%08" B_PRIx32 " readback after enable = 0x%08" B_PRIx32 "\n", 
			sCursorRegs.control, read32(sCursorRegs.control));

		// 4. Innesca l'hardware scrivendo l'indirizzo base (LATCH / Double Buffering)
		uint32 finalAddress = (uint32)gInfo->shared_info->physical_graphics_memory + gInfo->shared_info->cursor_buffer_offset;
		debug_printf("Intel Cursor Trace: Writing Base Address 0x%08" B_PRIx32 " to Reg 0x%08" B_PRIx32 "\n", finalAddress, sCursorRegs.base);
		write32(sCursorRegs.base, finalAddress);
		debug_printf("Intel Cursor Trace: Reg 0x%08" B_PRIx32 " readback after latch = 0x%08" B_PRIx32 "\n", 
			sCursorRegs.base, read32(sCursorRegs.base));

		// Aggiorna lo stato dell'hotspot interno di Haiku
		gInfo->shared_info->cursor_hot_x = hotX;
		gInfo->shared_info->cursor_hot_y = hotY;
		
		// Forziamo un movimento immediato
		int32 currentX = currentPos & 0xffff;
		int32 currentY = currentPos >> 16;
		if (currentX & CURSOR_POSITION_NEGATIVE) currentX = -(currentX & CURSOR_POSITION_MASK);
		if (currentY & CURSOR_POSITION_NEGATIVE) currentY = -(currentY & CURSOR_POSITION_MASK);
		
		debug_printf("Intel Cursor Trace: Forcing initial move_cursor to X: %d, Y: %d (HotSpot X: %d, Y: %d)\n", 
			currentX, currentY, hotX, hotY);
		intel_move_cursor(currentX + hotX, currentY + hotY);

		debug_printf("Intel Cursor Trace: --- END intel_set_cursor_bitmap (Gen9+) ---\n");
		return B_OK;
	}
	// -------------------------------------------------------------------------
	// RAMO 2: Architetture Legacy / Pre-Gen9 (Gen 4 / Gen 5 del tuo amico)
	// -------------------------------------------------------------------------

	// Vecchio stile: spegne il cursore prima delle modifiche
	write32(sCursorRegs.control, 0);

	uint32* dest = (uint32*)gInfo->shared_info->cursor_memory;
	const uint32* src = (const uint32*)bitmapData;
	uint32 srcPixelsPerRow = bytesPerRow / 4;

	// Pulisce il buffer per la vecchia scheda
	memset(dest, 0, 64 * 64 * sizeof(uint32));

	for (int32 y = 0; y < height; y++) {
		for (int32 x = 0; x < width; x++) {
			uint32 pixel = src[srcPixelsPerRow * y + x];
			if (colorSpace == B_RGB32)
				pixel |= 0xff000000;
			dest[64 * y + x] = pixel;
		}
	}

	// Applica il vecchio formato (senza bit 31 sulle vecchie Gen4/5)
	gInfo->shared_info->cursor_format = sCursorRegs.argb_mode_mask;

	write32(sCursorRegs.control, gInfo->shared_info->cursor_format);
	write32(sCursorRegs.size, (64 << 12) | 64);

	write32(sCursorRegs.base, (uint32)gInfo->shared_info->physical_graphics_memory
		+ gInfo->shared_info->cursor_buffer_offset);

	// Riposizionamento dinamico vecchio stile
	if (hotX != gInfo->shared_info->cursor_hot_x
		|| hotY != gInfo->shared_info->cursor_hot_y) {
		int32 x = read32(sCursorRegs.position);
		int32 y = x >> 16;
		x &= 0xffff;

		if (x & CURSOR_POSITION_NEGATIVE)
			x = -(x & CURSOR_POSITION_MASK);
		if (y & CURSOR_POSITION_NEGATIVE)
			y = -(y & CURSOR_POSITION_MASK);

		x += gInfo->shared_info->cursor_hot_x;
		y += gInfo->shared_info->cursor_hot_y;

		gInfo->shared_info->cursor_hot_x = hotX;
		gInfo->shared_info->cursor_hot_y = hotY;

		intel_move_cursor(x, y);
	}

	return B_OK;
}

void
intel_move_cursor(uint16 _x, uint16 _y)
{
    init_cursor_registers_layout(); // Sicurezza se viene chiamata prima di altre

    int32 x = (int32)_x - gInfo->shared_info->cursor_hot_x;
    int32 y = (int32)_y - gInfo->shared_info->cursor_hot_y;

    if (x < 0)
        x = -x | CURSOR_POSITION_NEGATIVE;
    if (y < 0)
        y = -y | CURSOR_POSITION_NEGATIVE;

    write32(sCursorRegs.position, (y << 16) | x);
}
void
intel_show_cursor(bool isVisible)
{
	init_cursor_registers_layout();

	if (gInfo->shared_info->cursor_visible == isVisible)
		return;

	if (!isVisible) {
		write32(sCursorRegs.control, 0); 
	} else {
		write32(sCursorRegs.control, gInfo->shared_info->cursor_format);
	}

	// Su Gen9+ riscrivere il registro base è obbligatorio per fare il "commit" dello stato (show/hide)
	write32(sCursorRegs.base, (uint32)gInfo->shared_info->physical_graphics_memory
		+ gInfo->shared_info->cursor_buffer_offset);

	gInfo->shared_info->cursor_visible = isVisible;
}
