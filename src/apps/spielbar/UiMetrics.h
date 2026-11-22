/*
 * SpielBar v3 - misure dell'interfaccia derivate dal font di sistema
 *
 * Generato da tools/split_app.py a partire da App.cpp (logica invariata).
 */

#ifndef SPIELBAR_UIMETRICS_H
#define SPIELBAR_UIMETRICS_H


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SpielBar"


// -------------------------------------------------------------
// Misure dell'interfaccia, tutte derivate dal font di sistema: con caratteri
// grandi (schermi ad alta densita', ipovisione) righe, pulsanti, icone e
// faccette crescono insieme al testo e non si sovrappongono. A 12 pt i valori
// coincidono con quelli storici (riga 44 px, pulsanti 48 px, ecc.).
// -------------------------------------------------------------

struct UiMetrics {
	float	fontSize;
	float	scale;			// fontSize / 12
	int		textHeight;		// altezza del testo normale
	int		buttonSize;		// pulsanti della barra superiore
	int		buttonIcon;
	int		emblemFacet;	// emblemi dei segmenti
	int		facetHeight;	// altezza comune dei controlli segmentati
	int		segPadText;
	int		segPadIcon;
	int		rowHeight;		// righe della lista
	int		rowPad;
	int		rowIcon;
	int		rowTextLeft;
	int		starSize;
	int		headerHeight;	// intestazioni di sezione
	int		handleHeight;	// barra di ridimensionamento
	float	nameBase;		// baseline del nome, dalla cima della riga
	float	pathBase;		// baseline del percorso
	float	emblemRise;		// quanto sopra la baseline si centrano gli emblemi
};

UiMetrics
ComputeUiMetrics();

// Calcolata al primo uso (serve il font di sistema, quindi l'app avviata).

const UiMetrics&
UI();

#endif	// SPIELBAR_UIMETRICS_H
