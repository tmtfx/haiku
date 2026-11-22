/*
 * SpielBar v3 - la lista unificata (hover, trascinamento, tasti)
 *
 * Generato da tools/split_app.py a partire da App.cpp (logica invariata).
 */

#ifndef SPIELBAR_UNIFIEDLISTVIEW_H
#define SPIELBAR_UNIFIEDLISTVIEW_H

#include <Bitmap.h>
#include <Font.h>
#include <GraphicsDefs.h>
#include <InterfaceDefs.h>
#include <ListItem.h>
#include <ListView.h>
#include <Message.h>
#include <Point.h>
#include <Rect.h>
#include <String.h>
#include <SupportDefs.h>
#include <Window.h>
#include <math.h>

#include "SpielBarDefs.h"
#include "SpielBarTypes.h"
#include "ListItems.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SpielBar"


class UnifiedListView : public BListView {
public:
	UnifiedListView(const char* name)
		: BListView(name)
		, fDragStart(-1, -1)
		, fDragging(false)
		, fDragIndex(-1)
		, fEmptyMessage("<empty>")
		, fHoverIndex(-1)
		, fStarsEnabled(false)
		, fHoverEnabled(true)
	{
		// Le righe sono disegnate rispetto al bordo destro (stella, testo
		// troncato): se la larghezza cambia va ridisegnato tutto, non solo
		// la parte nuova.
		SetFlags(Flags() | B_FULL_UPDATE_ON_RESIZE);
	}

	void SetEmptyMessage(const char* message)
	{
		fEmptyMessage = message;
		if (CountItems() == 0)
			Invalidate();
	}

	void SetStarsEnabled(bool enabled) { fStarsEnabled = enabled; }
	bool StarsEnabled() const { return fStarsEnabled; }

	void ResetHover()
	{
		fHoverIndex = -1;
		fDragIndex = -1;
		fDragging = false;
	}
	// Sospende l'evidenziazione al passaggio del mouse (durante il
	// trascinamento della maniglia il puntatore attraversa la lista).
	void SetHoverEnabled(bool enabled)
	{
		fHoverEnabled = enabled;
		if (!enabled) {
			int32 old = fHoverIndex;
			fHoverIndex = -1;
			if (old >= 0 && old < CountItems())
				InvalidateItem(old);
		}
	}

	BListItem* HoveredItem() const
	{
		return fHoverIndex >= 0 ? ItemAt(fHoverIndex) : NULL;
	}

	void MouseDown(BPoint where) override
	{
		BMessage* msg = Window()->CurrentMessage();
		int32 buttons = 0;
		msg->FindInt32("buttons", &buttons);
		int32 clicks = 0;
		msg->FindInt32("clicks", &clicks);
		int32 modifiers = 0;
		msg->FindInt32("modifiers", &modifiers);
		// Shift: lancia senza chiudere la finestra (piu' lanci di seguito).
		const bool keepOpen = (modifiers & B_SHIFT_KEY) != 0;

		int32 index = IndexOf(where);

		// Le intestazioni non si selezionano, non si lanciano, non si trascinano.
		if (index >= 0 && dynamic_cast<SectionHeaderItem*>(ItemAt(index)) != NULL)
			return;

		if (buttons & B_SECONDARY_MOUSE_BUTTON) {
			if (index >= 0) {
				Select(index);
				BMessage contextMsg(kMsgOpenMenu);
				contextMsg.AddPoint("where", where);
				contextMsg.AddInt32("index", index);
				contextMsg.AddBool("keep_open", keepOpen);
				Window()->PostMessage(&contextMsg);
			}
		} else if (buttons & B_PRIMARY_MOUSE_BUTTON) {
			if (index >= 0) {
				// Clic sulla stella: toggle, senza selezionare ne' trascinare.
				UnifiedListItem* item
					= dynamic_cast<UnifiedListItem*>(ItemAt(index));
				if (item != NULL && fStarsEnabled && item->Kind() != kKindVolume
					&& UnifiedListItem::StarRect(ItemFrame(index)).Contains(where)) {
					BMessage toggle(kMsgToggleFavorite);
					toggle.AddRef("refs", &item->Ref());
					Window()->PostMessage(&toggle);
					fDragStart = BPoint(-1, -1);
					fDragIndex = -1;
					fDragging = false;
					return;
				}

				if (clicks >= 2) {
					// Il secondo clic di un doppio clic non deve lanciare di
					// nuovo: il lancio e' gia' avvenuto al rilascio del primo
					// (MouseUp). Prima non si notava perche' la finestra si
					// chiudeva subito; con Shift resta aperta e il doppio
					// lancio si vedrebbe. Si ferma anche l'invocazione di
					// BListView, che userebbe l'InvocationMessage senza
					// modificatore.
					fDragStart = BPoint(-1, -1);
					fDragIndex = -1;
					fDragging = false;
					return;
				}

				fDragStart = where;
				fDragging = true;
				fDragIndex = index;
				SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
			}
			BListView::MouseDown(where);
		}
	}

	void MouseMoved(BPoint where, uint32 transit,
		const BMessage* dragMessage) override
	{
		// Hover: invalida solo le due righe interessate.
		int32 newHover = -1;
		if (transit != B_EXITED_VIEW && transit != B_OUTSIDE_VIEW
			&& dragMessage == NULL && fHoverEnabled)
			newHover = IndexOf(where);
		if (newHover >= 0 && dynamic_cast<UnifiedListItem*>(ItemAt(newHover)) == NULL)
			newHover = -1;
		if (newHover != fHoverIndex) {
			int32 old = fHoverIndex;
			fHoverIndex = newHover;
			if (old >= 0 && old < CountItems())
				InvalidateItem(old);
			if (newHover >= 0)
				InvalidateItem(newHover);
		}

		if (fDragging && !dragMessage && fDragIndex >= 0) {
			float distance = sqrtf(powf(where.x - fDragStart.x, 2)
				+ powf(where.y - fDragStart.y, 2));
			if (distance > 5.0) {
				UnifiedListItem* item
					= dynamic_cast<UnifiedListItem*>(ItemAt(fDragIndex));
				if (item && item->Kind() != kKindVolume) {
					BMessage drag(B_SIMPLE_DATA);
					drag.AddRef("refs", &item->Ref());
					drag.AddString("be:types", B_FILE_MIME_TYPE);
					drag.AddString("be:clip_name", item->Name());

					BBitmap* dragBitmap = nullptr;
					if (item->Icon()) {
						dragBitmap = new BBitmap(item->Icon());
					} else {
						dragBitmap = new BBitmap(BRect(0, 0, 31, 31), B_RGBA32);
					}

					DragMessage(&drag, dragBitmap, B_OP_ALPHA, BPoint(16, 16));
					fDragging = false;
				}
			}
		}
		BListView::MouseMoved(where, transit, dragMessage);
	}

	void Draw(BRect updateRect) override
	{
		BListView::Draw(updateRect);

		if (CountItems() == 0 && !fEmptyMessage.IsEmpty()) {
			SetHighColor(mix_color(ui_color(B_LIST_ITEM_TEXT_COLOR),
				ui_color(B_LIST_BACKGROUND_COLOR), 128));

			BFont font;
			GetFont(&font);
			font.SetFace(B_ITALIC_FACE);
			SetFont(&font);

			float stringWidth = StringWidth(fEmptyMessage.String());
			BRect bounds = Bounds();
			float x = (bounds.Width() - stringWidth) / 2;
			float y = bounds.Height() / 3;

			DrawString(fEmptyMessage.String(), BPoint(x, y));

			font.SetFace(B_REGULAR_FACE);
			SetFont(&font);
		}
	}

	void MouseUp(BPoint where) override
	{
		fDragging = false;
		if (fDragIndex >= 0 && fDragStart == where) {
			int32 index = IndexOf(where);
			fDragIndex = -1;
			if (index >= 0) {
				Select(index);
				int32 modifiers = 0;
				if (Window()->CurrentMessage() != NULL)
					Window()->CurrentMessage()->FindInt32("modifiers", &modifiers);
				BMessage launch(kMsgLaunch);
				launch.AddBool("keep_open", (modifiers & B_SHIFT_KEY) != 0);
				Window()->PostMessage(&launch);
			}
		}
		fDragIndex = -1;
		BListView::MouseUp(where);
	}

private:
	BPoint fDragStart;
	bool fDragging;
	int32 fDragIndex;
	BString fEmptyMessage;
	int32 fHoverIndex;
	bool fStarsEnabled;
	bool fHoverEnabled;
};

#endif	// SPIELBAR_UNIFIEDLISTVIEW_H
