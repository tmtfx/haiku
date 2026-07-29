/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef COLOR_CONVERSION_TABLE_H
#define COLOR_CONVERSION_TABLE_H

#include <MediaDefs.h>
#include <MediaRoster.h>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
}

// Struttura per mappare le rotte ottimali di conversione
struct ColorConversionRoute {
	enum AVPixelFormat native_format;
	std::vector<color_space> preferred_outputs; // Ordine: dal più efficiente al meno efficiente
};

// Tabella statica delle priorità basata sull'efficienza di conversione della CPU
// e sul risparmio di banda di memoria (VRAM/Bus)
static const ColorConversionRoute kConversionTable[] = {
	{
		AV_PIX_FMT_YUV422P, // Caso Theora / MPEG-2 4:2:2 Planare
		{
			B_YCbCr422, // 1° Scelta: Solo impaccamento byte (Nessun calcolo matematico, 16-bit)
			B_RGB16,    // 2° Scelta: Conversione a 16-bit (Metà banda rispetto a RGB32)
			B_RGB15,    // 3° Scelta: Conversione a 15-bit
			B_RGB32     // 4° Scelta: Fallback universale (Pesante, 32-bit)
		}
	},
	{
		AV_PIX_FMT_YUV420P, // Caso standard 4:2:0 Planare
		{
			B_YCbCr420, // 1° Scelta: Copia nativa (Se supportato dall'overlay hardware)
			B_YCbCr422, // 2° Scelta: Conversione/Up-sampling cromatico leggero
			B_RGB16,
			B_RGB32
		}
	},
	{
		AV_PIX_FMT_YUYV422, // Caso video già impaccato 4:2:2 (es. alcuni MOV)
		{
			B_YCbCr422, // 1° Scelta: Pass-through diretto (Zero calcoli CPU)
			B_RGB16,
			B_RGB32
		}
	}
	// Nota: Puoi estendere questa tabella per altri formati (es. AV_PIX_FMT_YUV444P, GRAY8, ecc.)
};

static const size_t kConversionTableSize = sizeof(kConversionTable) / sizeof(ColorConversionRoute);

/**
 * Trova il formato ottimale incrociando l'efficienza della CPU con le capacità reali della GPU.
 */
// Ritorna true se il formato è nativo hardware senza conversioni, false se richiede conversione software
static bool
FindBestHardwareFormat(enum AVPixelFormat nativeFormat, const std::vector<color_space>& hardwareSupportedSpaces, 
	color_space requestedSpace, color_space& outColorSpace)
{
	// 1. Cerca il formato nativo nella tabella di efficienza
	for (size_t i = 0; i < kConversionTableSize; ++i) {
		if (kConversionTable[i].native_format == nativeFormat) {
			
			for (size_t j = 0; j < kConversionTable[i].preferred_outputs.size(); ++j) {
				color_space preferredSpace = kConversionTable[i].preferred_outputs[j];
				
				for (size_t k = 0; k < hardwareSupportedSpaces.size(); ++k) {
					if (preferredSpace == hardwareSupportedSpaces[k]) {
						outColorSpace = preferredSpace;
						
						// Se il primo formato preferito della scheda coincide esattamente 
						// con il primo output ideale del formato nativo, non serve conversione pesante.
						if (j == 0 && preferredSpace == requestedSpace)
							return true; 
							
						return false; // Richiede conversione software attiva (es. da planare a impaccato)
					}
				}
			}
		}
	}
	
	// Fallback standard
	outColorSpace = B_RGB32;
	return false;
}


#endif // COLOR_CONVERSION_TABLE_H
