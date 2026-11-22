/*
 * SpielBar v3 - emblemi sovrapposti alle icone (preferiti, recenti, ...)
 *
 * Generato da tools/split_app.py a partire da App.cpp (logica invariata).
 */

#ifndef SPIELBAR_EMBLEMS_H
#define SPIELBAR_EMBLEMS_H

#include <Bitmap.h>
#include <Handler.h>
#include <Menu.h>
#include <Point.h>
#include <SupportDefs.h>
#include <View.h>
#include <math.h>
#include <stdio.h>
#include <algorithm>
#include <map>

#include "SpielBarDefs.h"
#include "UiMetrics.h"
#include "IconSupport.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SpielBar"


// -------------------------------------------------------------
// Emblemi (piano par. 2.5): icone HVIF dal .rdef, caricate una volta sola
// -------------------------------------------------------------

enum {
	kEmbFavorite = 0,
	kEmbFavoriteEmpty,
	kEmbRecent,
	kEmbSystem,
	kEmbPackage,
	kEmbSymlink,
	kEmbKindApp,
	kEmbKindPreflet,
	kEmbKindFile,
	kEmbKindFolder,
	kEmbCount
};

class EmblemCache {
public:
	~EmblemCache()
	{
		for (auto& pair : fBitmaps)
			delete pair.second;
	}

	// Dimensioni usate dalla lista (px, scalate col font).
	BBitmap* Get(int id) { return Get(id, _DefaultSize(id)); }

	// Rasterizza la HVIF esattamente a "size" px, una volta sola: una
	// bitmap disegnata a dimensione nativa resta nitida, una ridimensionata no.
	BBitmap* Get(int id, int size)
	{
		int key = id * 1000 + size;
		auto it = fBitmaps.find(key);
		if (it != fBitmaps.end())
			return it->second;

		BBitmap* bmp = LoadVectorIcon(_ResourceId(id), size);
		if (bmp == NULL) {
			fprintf(stderr, "SpielBar: icon res %d not found\n",
				(int)_ResourceId(id));
		}
		fBitmaps[key] = bmp;	// anche NULL: non si riprova
		return bmp;
	}

private:
	static int32 _ResourceId(int id)
	{
		static const int32 ids[kEmbCount] = {
			kIconEmblemFavorite, kIconEmblemFavoriteEmpty, kIconEmblemRecent,
			kIconEmblemSystem, kIconEmblemPackage, kIconEmblemSymlink,
			kIconKindApp, kIconKindPreflet, kIconKindFile, kIconKindFolder
		};
		return ids[id];
	}

	static int _DefaultSize(int id)
	{
		static const int base[kEmbCount] = { 18, 18, 12, 14, 14, 16, 16, 16, 16, 16 };
		return std::max(8, (int)roundf(base[id] * UI().scale));
	}

	std::map<int, BBitmap*> fBitmaps;
};

EmblemCache&
Emblems();

void
DestroyEmblems();

// Disegna un emblema (se manca nel .rdef, semplicemente non disegna nulla).

void
DrawEmblem(BView* owner, int id, BPoint at);

float
EmblemWidth(int id);

// -------------------------------------------------------------
// Pulsanti della barra superiore
// -------------------------------------------------------------

// Imposta il target su tutte le voci, sottomenu compresi:
// SetTargetForItems() non scende nei sottomenu.

void
SetMenuTarget(BMenu* menu, BHandler* target);

#endif	// SPIELBAR_EMBLEMS_H
