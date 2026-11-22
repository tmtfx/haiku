/*
 * SpielBar v3 - emblemi sovrapposti alle icone (preferiti, recenti, ...)
 *
 * Generato da tools/split_app.py a partire da App.cpp (logica invariata).
 */

#include <Bitmap.h>
#include <Handler.h>
#include <Menu.h>
#include <MenuItem.h>
#include <Point.h>
#include <SupportDefs.h>
#include <View.h>

#include "Emblems.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SpielBar"


// La cache NON e' un oggetto statico: un distruttore statico girerebbe dopo
// la fine di main(), quando la connessione all'app_server e' gia' chiusa
// (crash in ~BBitmap). La distrugge ~DeskbarApp, ad app ancora viva.
static EmblemCache* gEmblems = NULL;

EmblemCache&
Emblems()
{
	if (gEmblems == NULL)
		gEmblems = new EmblemCache();
	return *gEmblems;
}

void
DestroyEmblems()
{
	delete gEmblems;
	gEmblems = NULL;
}

void
DrawEmblem(BView* owner, int id, BPoint at)
{
	BBitmap* bmp = Emblems().Get(id);
	if (bmp == NULL)
		return;
	owner->SetDrawingMode(B_OP_ALPHA);
	owner->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
	owner->DrawBitmap(bmp, at);
	owner->SetDrawingMode(B_OP_COPY);
}

float
EmblemWidth(int id)
{
	BBitmap* bmp = Emblems().Get(id);
	return bmp != NULL ? bmp->Bounds().Width() + 1 : 0;
}

void
SetMenuTarget(BMenu* menu, BHandler* target)
{
	for (int32 i = 0; i < menu->CountItems(); i++) {
		BMenuItem* item = menu->ItemAt(i);
		if (BMenu* submenu = item->Submenu())
			SetMenuTarget(submenu, target);
		else
			item->SetTarget(target);
	}
}
