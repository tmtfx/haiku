/*
 * SpielBar v3 - maniglia di ridimensionamento e contorno fantasma
 *
 * Generato da tools/split_app.py a partire da App.cpp (logica invariata).
 */

#ifndef SPIELBAR_RESIZEHANDLE_H
#define SPIELBAR_RESIZEHANDLE_H

#include <Cursor.h>
#include <GraphicsDefs.h>
#include <InterfaceDefs.h>
#include <Message.h>
#include <Point.h>
#include <Rect.h>
#include <Size.h>
#include <SupportDefs.h>
#include <View.h>
#include <Window.h>
#include <math.h>
#include <algorithm>
#include <functional>

#include "SpielBarTheme.h"
#include "UiMetrics.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SpielBar"


// -------------------------------------------------------------
// Barra di ridimensionamento verticale (la finestra e' senza bordo)
// -------------------------------------------------------------

// Barra a tutta larghezza con un'impugnatura a capsula centrata. Il
// trascinamento dice alla finestra la nuova altezza; il doppio clic
// ripristina l'altezza predefinita. atBottom: la barra sta in fondo alla
// finestra (trascinare in basso allunga); altrimenti sta in cima.
class ResizeHandle : public BView {
public:
	ResizeHandle(bool atBottom, std::function<void(float, bool)> resize,
			std::function<void()> reset)
		: BView("resize_handle", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE)
		, fAtBottom(atBottom)
		, fResize(resize)
		, fReset(reset)
		, fTracking(false)
		, fMoved(false)
		, fHover(false)
		, fStartY(0)
		, fStartHeight(0)
	{
		SetViewColor(B_TRANSPARENT_COLOR);
		float height = (float)UI().handleHeight;
		SetExplicitMinSize(BSize(B_SIZE_UNSET, height));
		SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, height));
		SetExplicitPreferredSize(BSize(B_SIZE_UNSET, height));
	}

	void AttachedToWindow() override
	{
		BView::AttachedToWindow();
		BCursor cursor(B_CURSOR_ID_RESIZE_NORTH_SOUTH);
		SetViewCursor(&cursor);
	}

	void Draw(BRect updateRect) override
	{
		BRect b = Bounds();
		bool active = fHover || fTracking;

		// A riposo: sfondo del pannello e impugnatura del colore del testo
		// del pannello, attenuato verso lo sfondo (schiarito su tema chiaro,
		// scurito su tema scuro). Al passaggio del mouse (e trascinando):
		// sfondo della tab e impugnatura col colore del testo della tab.
		rgb_color panel = ui_color(B_PANEL_BACKGROUND_COLOR);
		rgb_color background = active ? ui_color(B_WINDOW_TAB_COLOR) : panel;
		rgb_color gripColor = active ? ui_color(B_WINDOW_TEXT_COLOR)
			: tint_color(ui_color(B_PANEL_TEXT_COLOR),
				panel.IsLight() ? B_LIGHTEN_2_TINT : B_DARKEN_2_TINT);

		SetDrawingMode(B_OP_COPY);
		SetHighColor(background);
		FillRect(b);

		// Linea sottile dal lato che confina con la lista.
		SetHighColor(ContrastShade(background, 1));
		if (fAtBottom)
			StrokeLine(b.LeftTop(), b.RightTop());
		else
			StrokeLine(b.LeftBottom(), b.RightBottom());

		// Impugnatura: capsula centrata.
		float fontSize = UI().fontSize;
		float gripW = roundf(fontSize * 3.2f);
		float gripH = std::max(4.0f, roundf(fontSize * 0.35f));
		BRect grip(0, 0, gripW - 1, gripH - 1);
		grip.OffsetTo(floorf((b.Width() + 1 - gripW) / 2),
			floorf((b.Height() + 1 - gripH) / 2));

		gripColor.alpha = 255;
		SetDrawingMode(B_OP_ALPHA);
		SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
		SetHighColor(gripColor);
		FillRoundRect(grip, gripH / 2, gripH / 2);
		SetDrawingMode(B_OP_COPY);
	}

	void MouseDown(BPoint where) override
	{
		int32 buttons = 0;
		int32 clicks = 1;
		BMessage* current = Window()->CurrentMessage();
		if (current != NULL) {
			current->FindInt32("buttons", &buttons);
			current->FindInt32("clicks", &clicks);
		}
		if (!(buttons & B_PRIMARY_MOUSE_BUTTON))
			return;

		if (clicks >= 2) {
			if (fReset)
				fReset();
			return;
		}

		// Coordinate dello schermo: la finestra si muove mentre si trascina
		// (quando e' ancorata in basso) e quelle della view cambierebbero.
		fTracking = true;
		fMoved = false;
		fStartY = _ScreenY(where);
		fStartHeight = Window()->Frame().Height();
		SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
		Invalidate();
	}

	void MouseMoved(BPoint where, uint32 transit, const BMessage*) override
	{
		if (fTracking) {
			// Rilascio perso (il mouse e' stato rilasciato altrove): il gesto
			// termina, altrimenti il focus della finestra resterebbe bloccato
			// e non si chiuderebbe piu' perdendo l'attivazione.
			int32 buttons = 0;
			BMessage* current = Window()->CurrentMessage();
			if (current != NULL)
				current->FindInt32("buttons", &buttons);
			if (buttons == 0) {
				MouseUp(where);
				return;
			}

			float delta = _ScreenY(where) - fStartY;
			fResize(fStartHeight + (fAtBottom ? delta : -delta), false);
			fMoved = true;
			return;
		}
		bool inside = transit != B_EXITED_VIEW && transit != B_OUTSIDE_VIEW;
		if (inside != fHover) {
			fHover = inside;
			Invalidate();
		}
	}

	void MouseUp(BPoint) override
	{
		if (!fTracking)
			return;
		fTracking = false;
		if (fMoved)
			fResize(0, true);	// conferma: salva la nuova altezza
		fMoved = false;
		Invalidate();
	}

private:
	// Posizione verticale del puntatore sullo schermo, presa dal messaggio:
	// ConvertToScreen() usa la posizione della finestra nota al client, che
	// durante il trascinamento (finestra ancorata in basso, quindi in
	// movimento) non coincide ancora con quella reale e farebbe oscillare.
	float _ScreenY(BPoint where)
	{
		BPoint screenWhere;
		BMessage* current = Window()->CurrentMessage();
		if (current != NULL
			&& current->FindPoint("screen_where", &screenWhere) == B_OK)
			return screenWhere.y;
		return ConvertToScreen(where).y;
	}

	bool fAtBottom;
	std::function<void(float, bool)> fResize;
	std::function<void()> fReset;
	bool fTracking;
	bool fMoved;
	bool fHover;
	float fStartY;
	float fStartHeight;
};

// Linea fantasma mostrata durante il trascinamento della maniglia: una
// finestra sottilissima, senza bordo, che non prende mai il focus (se lo
// prendesse, SpielBar perderebbe l'attivazione e si chiuderebbe).
class GhostView : public BView {
public:
	GhostView(BRect frame)
		: BView(frame, "ghost", B_FOLLOW_ALL, B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE)
	{
		SetViewColor(B_TRANSPARENT_COLOR);
	}

	void Draw(BRect) override
	{
		BRect b = Bounds();
		rgb_color base = ui_color(B_WINDOW_TAB_COLOR);
		SetDrawingMode(B_OP_COPY);
		SetHighColor(ContrastShade(base, 2));
		FillRect(b);
		// Un bordo scuro di 1 px; l'interno solo se c'e' spazio (le linee
		// sottili restano tutte del colore del bordo).
		if (b.Width() >= 2 && b.Height() >= 2) {
			SetHighColor(base);
			FillRect(b.InsetByCopy(1, 1));
		}
	}
};

class ResizeGhost : public BWindow {
public:
	ResizeGhost(BRect frame)
		: BWindow(frame, "resize_ghost", B_NO_BORDER_WINDOW_LOOK,
			B_FLOATING_ALL_WINDOW_FEEL,
			B_AVOID_FOCUS | B_NOT_CLOSABLE | B_NOT_ZOOMABLE | B_NOT_MINIMIZABLE)
	{
		SetSizeLimits(1, 16384, 1, 16384);
		SetWorkspaces(B_ALL_WORKSPACES);
		AddChild(new GhostView(Bounds()));
	}
};

#endif	// SPIELBAR_RESIZEHANDLE_H
