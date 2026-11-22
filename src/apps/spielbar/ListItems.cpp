/*
 * SpielBar v3 - elementi della lista: intestazioni, voci, volumi
 *
 * Generato da tools/split_app.py a partire da App.cpp (logica invariata).
 */

#include <Font.h>
#include <GraphicsDefs.h>
#include <InterfaceDefs.h>
#include <Point.h>
#include <Rect.h>
#include <String.h>
#include <View.h>
#include <math.h>

#include "ListItems.h"
#include "SpielBarTypes.h"
#include "SpielBarTheme.h"
#include "UiMetrics.h"
#include "Emblems.h"
#include "UnifiedListView.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SpielBar"


void
UnifiedListItem::DrawItem(BView* owner, BRect frame, bool complete)
{
	const UiMetrics& ui = UI();
	UnifiedListView* list = dynamic_cast<UnifiedListView*>(owner);
	bool isVolume = (fKind == kKindVolume);
	bool showStar = list != NULL && list->StarsEnabled() && !isVolume;
	bool hideRecent = fHideRecent;
	bool hovered = list != NULL && list->HoveredItem() == this;

	rgb_color textColor = ui_color(B_LIST_ITEM_TEXT_COLOR);
	rgb_color bg = ui_color(B_LIST_BACKGROUND_COLOR);
	if (IsSelected()) {
		bg = ui_color(B_LIST_SELECTED_BACKGROUND_COLOR);
		textColor = ui_color(B_LIST_SELECTED_ITEM_TEXT_COLOR);
	} else if (hovered) {
		bg = ContrastShade(bg, 1);
	}
	owner->SetDrawingMode(B_OP_COPY);
	owner->SetHighColor(bg);
	owner->FillRect(frame);
	owner->SetLowColor(bg);

	// Icona centrata verticalmente nella riga.
	if (fIcon && fIcon->IsValid()) {
		float iconTop = floorf(frame.top + (frame.Height() + 1 - ui.rowIcon) / 2);
		BRect iconRect(frame.left + ui.rowPad, iconTop,
			frame.left + ui.rowPad + ui.rowIcon - 1, iconTop + ui.rowIcon - 1);
		owner->SetDrawingMode(B_OP_ALPHA);
		owner->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
		owner->DrawBitmap(fIcon, fIcon->Bounds(), iconRect);
		owner->SetDrawingMode(B_OP_COPY);

		// Overlay symlink in basso a sinistra sull'icona.
		if (!isVolume && (fItemFlags & kFlagSymlink)) {
			float s = EmblemWidth(kEmbSymlink);
			DrawEmblem(owner, kEmbSymlink,
				BPoint(iconRect.left, iconRect.bottom - s + 1));
		}
	}

	// Emblemi dopo il nome: sistema/pacchetto, poi recente.
	int emblems[2];
	int emblemCount = 0;
	if (!isVolume) {
		if (fItemFlags & kFlagSystem)
			emblems[emblemCount++] = kEmbSystem;
		else if (fItemFlags & kFlagPackaged)
			emblems[emblemCount++] = kEmbPackage;
		if ((fItemFlags & kFlagRecent) && !hideRecent)
			emblems[emblemCount++] = kEmbRecent;
	}
	float emblemsWidth = 0;
	for (int i = 0; i < emblemCount; i++)
		emblemsWidth += EmblemWidth(emblems[i]) + 4;

	float textX = frame.left + ui.rowTextLeft;
	float starWidth = showStar ? StarRect(frame).Width() + 1 : 0;
	float maxTextWidth = frame.Width() + 1 - ui.rowTextLeft
		- (showStar ? ui.rowPad + 4 + starWidth + 8 : 4);

	BFont boldFont(be_plain_font);
	boldFont.SetFace(B_BOLD_FACE);
	owner->SetFont(&boldFont);
	owner->SetHighColor(textColor);

	// Baseline calcolate dalle metriche del font (UiMetrics).
	float nameY = frame.top + ui.nameBase;
	BString truncatedName(fName);
	owner->TruncateString(&truncatedName, B_TRUNCATE_END,
		maxTextWidth - emblemsWidth);
	owner->DrawString(truncatedName.String(), BPoint(textX, nameY));

	float ex = textX + boldFont.StringWidth(truncatedName.String()) + ui.rowPad;
	for (int i = 0; i < emblemCount; i++) {
		float size = EmblemWidth(emblems[i]);
		// Coordinate intere: una bitmap a mezzo pixel viene sfocata.
		DrawEmblem(owner, emblems[i], BPoint(floorf(ex),
			floorf(nameY - ui.emblemRise - size / 2)));
		ex += size + 4;
	}

	BFont italicFont(be_plain_font);
	italicFont.SetFace(B_ITALIC_FACE);
	owner->SetFont(&italicFont);
	// Il percorso e' un po' piu' tenue del nome (tranne sulla riga selezionata).
	owner->SetHighColor(IsSelected() ? textColor : mix_color(textColor, bg, 70));

	float pathY = frame.top + ui.pathBase;
	BString truncatedPath(fPath);
	owner->TruncateString(&truncatedPath, B_TRUNCATE_MIDDLE, maxTextWidth);
	owner->DrawString(truncatedPath.String(), BPoint(textX, pathY));

	owner->SetFont(be_plain_font);

	// Stella: piena se preferito, vuota solo in hover/selezione.
	if (showStar) {
		if (fItemFlags & kFlagFavorite)
			DrawEmblem(owner, kEmbFavorite, StarRect(frame).LeftTop());
		else if (hovered || IsSelected())
			DrawEmblem(owner, kEmbFavoriteEmpty, StarRect(frame).LeftTop());
	}
}
