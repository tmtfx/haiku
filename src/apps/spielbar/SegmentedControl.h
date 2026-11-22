/*
 * SpielBar v3 - controllo segmentato a faccette
 *
 * Generato da tools/split_app.py a partire da App.cpp (logica invariata).
 */

#ifndef SPIELBAR_SEGMENTEDCONTROL_H
#define SPIELBAR_SEGMENTEDCONTROL_H

#include <Bitmap.h>
#include <ControlLook.h>
#include <GraphicsDefs.h>
#include <Handler.h>
#include <InterfaceDefs.h>
#include <Message.h>
#include <Messenger.h>
#include <Point.h>
#include <Rect.h>
#include <Size.h>
#include <String.h>
#include <SupportDefs.h>
#include <View.h>
#include <math.h>
#include <vector>

#include "SpielBarTheme.h"
#include "UiMetrics.h"
#include "Emblems.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SpielBar"


// -------------------------------------------------------------
// Chip delle faccette, campo con segnaposto, legenda
// -------------------------------------------------------------

// Controllo segmentato a selezione multipla: ( a | b | c ), ogni segmento
// e' un bit della maschera. Un segmento ha un testo oppure un emblema
// (se l'emblema manca nel .rdef si ripiega sul testo). I segmenti si
// disabilitano in blocco, ma la selezione resta visibile. Le misure
// vengono da UI(): tutti i controlli segmentati hanno la stessa altezza.
class SegmentedControl : public BView {
public:
	SegmentedControl(const char* name, uint32 what, const char* field)
		: BView(name, B_WILL_DRAW | B_FRAME_EVENTS)
		, fWhat(what)
		, fField(field)
		, fActive(0)
		, fEnabled(true)
		, fHover(-1)
	{
		SetViewColor(B_TRANSPARENT_COLOR);
	}

	void AddSegment(uint32 bit, const char* label, const char* tip,
		int emblem = -1)
	{
		Segment s;
		s.bit = bit;
		s.label = label;
		s.tip = tip;
		s.emblem = emblem;
		fSegments.push_back(s);
		InvalidateLayout();
	}

	void SetTarget(BHandler* handler) { fMessenger = BMessenger(handler); }

	void SetActiveMask(uint32 mask)
	{
		if (mask != fActive) {
			fActive = mask;
			Invalidate();
		}
	}

	void SetSegmentsEnabled(bool enabled)
	{
		if (enabled != fEnabled) {
			fEnabled = enabled;
			Invalidate();
		}
	}

	void GetPreferredSize(float* width, float* height) override
	{
		BSize size = _TotalSize();
		if (width)
			*width = size.width;
		if (height)
			*height = size.height;
	}

	BSize MinSize() override { return _TotalSize(); }
	BSize MaxSize() override { return _TotalSize(); }

	void Draw(BRect updateRect) override
	{
		rgb_color panel = ui_color(B_PANEL_BACKGROUND_COLOR);
		rgb_color base = ui_color(B_CONTROL_BACKGROUND_COLOR);
		SetHighColor(panel);
		FillRect(Bounds());

		// Bit dei bordi di BControlLook (left, right, top, bottom).
		const uint32 kLeft = 1 << 0, kRight = 1 << 1, kTop = 1 << 2,
			kBottom = 1 << 3;

		float x = 0;
		for (size_t i = 0; i < fSegments.size(); i++) {
			float w = _SegmentWidth(i);
			BRect r(x, 0, x + w - 1, Bounds().bottom);
			x += w;

			// Ogni segmento disegna il proprio bordo destro; il primo anche
			// il sinistro: niente doppi bordi tra segmenti adiacenti.
			uint32 borders = kTop | kBottom | kRight;
			if (i == 0)
				borders |= kLeft;

			uint32 flags = 0;
			if (fActive & fSegments[i].bit)
				flags |= BControlLook::B_ACTIVATED;
			if (!fEnabled)
				flags |= BControlLook::B_DISABLED;

			BRect content = r;
			be_control_look->DrawButtonFrame(this, content, updateRect, base,
				panel, flags, borders);
			be_control_look->DrawButtonBackground(this, content, updateRect,
				base, flags, borders);

			_DrawContent(fSegments[i], r);
		}
	}

	void MouseDown(BPoint where) override
	{
		if (!fEnabled)
			return;
		int32 index = _SegmentAt(where);
		if (index < 0)
			return;
		BMessage msg(fWhat);
		msg.AddInt32(fField.String(), (int32)fSegments[index].bit);
		fMessenger.SendMessage(&msg);
	}

	void MouseMoved(BPoint where, uint32 transit, const BMessage*) override
	{
		int32 index = (transit == B_EXITED_VIEW || transit == B_OUTSIDE_VIEW)
			? -1 : _SegmentAt(where);
		if (index != fHover) {
			fHover = index;
			if (index >= 0)
				SetToolTip(fSegments[index].tip.String());
		}
	}

private:
	struct Segment {
		uint32	bit;
		BString	label;
		BString	tip;
		int		emblem;
	};

	bool _HasEmblem(const Segment& s) const
	{
		return s.emblem >= 0
			&& Emblems().Get(s.emblem, UI().emblemFacet) != NULL;
	}

	float _SegmentWidth(size_t i) const
	{
		const Segment& s = fSegments[i];
		if (_HasEmblem(s))
			return UI().emblemFacet + UI().segPadIcon;
		return ceilf(StringWidth(s.label.String())) + UI().segPadText;
	}

	BSize _TotalSize() const
	{
		float width = 0;
		for (size_t i = 0; i < fSegments.size(); i++)
			width += _SegmentWidth(i);
		return BSize(width, (float)UI().facetHeight);
	}

	int32 _SegmentAt(BPoint where) const
	{
		float x = 0;
		for (size_t i = 0; i < fSegments.size(); i++) {
			float w = _SegmentWidth(i);
			if (where.x >= x && where.x < x + w)
				return (int32)i;
			x += w;
		}
		return -1;
	}

	void _DrawContent(const Segment& s, BRect r)
	{
		if (_HasEmblem(s)) {
			const int size = UI().emblemFacet;
			BBitmap* bmp = Emblems().Get(s.emblem, size);

			// Coordinate intere: una bitmap a mezzo pixel viene sfocata.
			BPoint at(floorf(r.left + (r.Width() + 1 - size) / 2),
				floorf(r.top + (r.Height() + 1 - size) / 2));

			SetDrawingMode(B_OP_ALPHA);
			if (fEnabled) {
				SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
			} else {
				SetHighColor(0, 0, 0, 90);
				SetBlendingMode(B_CONSTANT_ALPHA, B_ALPHA_OVERLAY);
			}
			DrawBitmap(bmp, at);	// nessun ridimensionamento
			SetDrawingMode(B_OP_COPY);
			return;
		}

		// Senza icona (risorsa mancante) il segmento mostra il testo.
		font_height fh;
		GetFontHeight(&fh);
		rgb_color text = ui_color(B_CONTROL_TEXT_COLOR);
		if (!fEnabled)
			text = DisabledText(text,
				ui_color(B_CONTROL_BACKGROUND_COLOR));
		SetHighColor(text);
		float w = StringWidth(s.label.String());
		DrawString(s.label.String(), BPoint(r.left + (r.Width() + 1 - w) / 2,
			r.top + (r.Height() + 1 + fh.ascent - fh.descent) / 2));
	}

	std::vector<Segment>	fSegments;
	uint32					fWhat;
	BString					fField;
	uint32					fActive;
	bool					fEnabled;
	int32					fHover;
	BMessenger				fMessenger;
};

#endif	// SPIELBAR_SEGMENTEDCONTROL_H
