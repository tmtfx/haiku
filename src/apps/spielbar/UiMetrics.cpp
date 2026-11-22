/*
 * SpielBar v3 - misure dell'interfaccia derivate dal font di sistema
 *
 * Generato da tools/split_app.py a partire da App.cpp (logica invariata).
 */

#include <Font.h>
#include <math.h>
#include <algorithm>

#include "UiMetrics.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SpielBar"


UiMetrics
ComputeUiMetrics()
{
	UiMetrics m;
	BFont plain(be_plain_font);
	font_height fh;
	plain.GetHeight(&fh);

	m.fontSize = plain.Size();
	m.scale = m.fontSize / 12.0f;
	m.textHeight = (int)ceilf(fh.ascent + fh.descent);

	m.buttonSize = std::max(32, (int)roundf(m.fontSize * 4));
	m.buttonIcon = (m.buttonSize * 2 + 1) / 3;

	// Faccette: un'unica altezza per tutti i controlli segmentati, anche
	// quando solo alcuni hanno gli emblemi.
	m.emblemFacet = std::max(16, (int)ceilf(m.textHeight * 1.1f));
	m.facetHeight = std::max(m.textHeight, m.emblemFacet)
		+ (int)roundf(m.fontSize * 0.8f);
	m.segPadText = (int)roundf(m.fontSize * 1.8f);
	m.segPadIcon = (int)roundf(m.fontSize * 1.7f);

	// Lista: l'altezza della riga e le baseline dipendono dalle metriche
	// dei font usati (grassetto per il nome, corsivo per il percorso).
	BFont bold(be_plain_font);
	bold.SetFace(B_BOLD_FACE);
	BFont italic(be_plain_font);
	italic.SetFace(B_ITALIC_FACE);
	font_height fb, fi;
	bold.GetHeight(&fb);
	italic.GetHeight(&fi);

	float nameH = ceilf(fb.ascent + fb.descent);
	float pathH = ceilf(fi.ascent + fi.descent);
	float gap = (float)std::max(1, (int)roundf(m.fontSize * 0.15f));
	float block = nameH + gap + pathH;

	m.rowPad = std::max(4, (int)roundf(m.fontSize * 0.5f));
	m.rowIcon = m.scale >= 1.5f
		? 16 * (int)roundf(32.0f * m.scale / 16.0f) : 32;
	m.rowHeight = (int)ceilf(std::max((float)m.rowIcon, block) + 2 * m.rowPad);
	m.rowTextLeft = m.rowPad + m.rowIcon + m.rowPad;

	float top = (m.rowHeight - block) / 2;
	m.nameBase = floorf(top + fb.ascent);
	m.pathBase = floorf(top + nameH + gap + fi.ascent);
	m.emblemRise = fb.ascent * 0.5f;

	m.starSize = std::max(14, (int)roundf(18 * m.scale));
	m.headerHeight = (int)ceilf((fh.ascent + fh.descent) * 0.85f)
		+ (int)roundf(m.fontSize * 0.9f);
	m.handleHeight = std::max(10, (int)roundf(m.fontSize * 0.9f));
	return m;
}

const UiMetrics&
UI()
{
	static UiMetrics metrics = ComputeUiMetrics();
	return metrics;
}
