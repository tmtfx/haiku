/*
 * SpielBar v3 - campo di filtro con testo segnaposto
 *
 * Generato da tools/split_app.py a partire da App.cpp (logica invariata).
 */

#ifndef SPIELBAR_PLACEHOLDERTEXTCONTROL_H
#define SPIELBAR_PLACEHOLDERTEXTCONTROL_H

#include <GraphicsDefs.h>
#include <InterfaceDefs.h>
#include <Message.h>
#include <Point.h>
#include <Rect.h>
#include <String.h>
#include <TextControl.h>
#include <TextView.h>
#include <math.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SpielBar"


class PlaceholderTextControl : public BTextControl {
public:
	PlaceholderTextControl(const char* name, BMessage* message)
		: BTextControl(name, "", "", message) {}

	void SetPlaceholder(const char* text)
	{
		fPlaceholder = text;
		Invalidate();
	}

	// BTextControl non ha un segnaposto nativo (verificare par. 9.4).
	void DrawAfterChildren(BRect updateRect) override
	{
		BTextControl::DrawAfterChildren(updateRect);
		if (fPlaceholder.IsEmpty() || Text()[0] != '\0')
			return;

		BTextView* tv = TextView();
		BRect textRect = tv->TextRect();
		font_height fh;
		tv->GetFontHeight(&fh);
		BPoint p = tv->ConvertToParent(
			BPoint(textRect.left, textRect.top + ceilf(fh.ascent)));
		SetFont(be_plain_font);
		SetHighColor(mix_color(ui_color(B_DOCUMENT_TEXT_COLOR),
			ui_color(B_DOCUMENT_BACKGROUND_COLOR), 128));
		DrawString(fPlaceholder.String(), p);
	}

private:
	BString fPlaceholder;
};

#endif	// SPIELBAR_PLACEHOLDERTEXTCONTROL_H
