/*
 * SpielBar v3 - posizione della finestra rispetto alla Deskbar
 *
 * Generato da tools/split_app.py a partire da App.cpp (logica invariata).
 */

#include <AppDefs.h>
#include <Deskbar.h>
#include <Message.h>
#include <Messenger.h>
#include <Point.h>
#include <Rect.h>
#include <Screen.h>
#include <SupportDefs.h>
#include <math.h>
#include <algorithm>

#include "DeskbarLayout.h"
#include "SpielBarDefs.h"
#include "SpielBarTypes.h"
#include "UiMetrics.h"
#include "SettingsStore.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SpielBar"


BRect
GetDeskbarFrame()
{
	BRect frame;
	bool found = false;
	BMessenger messenger("application/x-vnd.Be-TSKB");

	if (messenger.IsValid()) {
		BMessage reply;
		BMessage request(B_GET_PROPERTY);
		request.AddSpecifier("Frame");
		request.AddSpecifier("Window", (int32)0);

		found = messenger.SendMessage(&request, &reply) == B_OK
			&& reply.FindRect("result", &frame) == B_OK;
	}
	if (!found)
		frame = BDeskbar().Frame();
	return frame;
}

bool
GetDeskbarShelfTop(float* top)
{
	BMessenger messenger("application/x-vnd.Be-TSKB");
	if (!messenger.IsValid())
		return false;

	BMessage reply;
	BMessage request(B_GET_PROPERTY);
	request.AddSpecifier("Frame");
	request.AddSpecifier("View", (int32)1);
	request.AddSpecifier("View", (int32)0);
	request.AddSpecifier("Window", (int32)0);

	BRect frame;
	if (messenger.SendMessage(&request, &reply) != B_OK
		|| reply.FindRect("result", &frame) != B_OK)
		return false;

	*top = frame.top;
	return true;
}

bool
IsDeskbarAtTop()
{
	return GetDeskbarFrame().top <= BScreen().Frame().top + 0.5f;
}

BPoint
CalculateAppPosition(BRect windowRect)
{
	const float kEdge = 1;	// distanza dal bordo dello schermo
	const float kGap = 3;	// distanza dalla Deskbar

	BRect db = GetDeskbarFrame();
	BRect screen = BScreen().Frame();
	const float w = windowRect.Width() + 1;
	const float h = windowRect.Height() + 1;

	bool atLeft = db.left <= screen.left + 0.5f;
	bool fullWidth = atLeft && db.right >= screen.right - 0.5f;
	bool atRight = !atLeft;
	bool atTop = db.top <= screen.top + 0.5f;

	float shelfTop = 0;
	bool haveShelf = GetDeskbarShelfTop(&shelfTop);
	bool row;
	if (fullWidth)
		row = true;
	else if (haveShelf)
		row = shelfTop < 1;
	else
		row = db.Width() > 2 * db.Height();	// ripiego senza scripting

	BPoint p;
	if (fullWidth) {
		// Espansa in alto/in basso: sotto/sopra la barra, a sinistra.
		p.x = kEdge;
		p.y = atTop ? db.bottom + kGap : db.top - h - (kGap - 1);
	} else if (row) {
		// Mini: sotto/sopra la striscia, dal lato in cui sta.
		p.x = atRight ? screen.right + 1 - w - kEdge : screen.left + kEdge;
		p.y = atTop ? db.bottom + kGap : db.top - h - (kGap - 1);
	} else {
		// Colonna (compatta o espansa di lato): accanto alla barra.
		p.x = atRight ? db.left - w - (kGap - 1) : db.right + kGap;
		p.y = atTop ? screen.top + kEdge : screen.bottom + 1 - h - kEdge;
	}

	// Mai fuori dallo schermo.
	p.x = std::max(screen.left, std::min(p.x, screen.right + 1 - w));
	p.y = std::max(screen.top, std::min(p.y, screen.bottom + 1 - h));
	return p;
}

BRect
InitialWindowFrame()
{
	const UiMetrics& ui = UI();
	Settings settings;
	SettingsLoad(settings);

	float units = settings.windowHeightUnits > 0
		? settings.windowHeightUnits : kDefaultHeightUnits;
	float width = roundf(ui.fontSize * kDefaultWidthUnits);
	float height = roundf(ui.fontSize * units);
	height = std::min(height, BScreen().Frame().Height() - 40);
	return BRect(-1 - width, -1 - height, -1, -1);
}
