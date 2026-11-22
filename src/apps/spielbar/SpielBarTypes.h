/*
 * SpielBar v3 - modello dei dati: mondi, tipi, tag, stato, impostazioni
 *
 * Generato da tools/split_app.py a partire da App.cpp (logica invariata).
 */

#ifndef SPIELBAR_SPIELBARTYPES_H
#define SPIELBAR_SPIELBARTYPES_H

#include <Node.h>
#include <String.h>
#include <SupportDefs.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SpielBar"


// -------------------------------------------------------------
// Modello (piano par. 3.1)
// -------------------------------------------------------------

enum World { kWorldCatalog = 0, kWorldDisks, kWorldGo, kWorldSearch };

enum Kind {
	kKindApp = 1 << 0,
	kKindPreflet = 1 << 1,
	kKindFile = 1 << 2,
	kKindFolder = 1 << 3,
	kKindVolume = 1 << 4	// solo mondo Dischi
};

static const uint32 kKindFilterMask = 0x0F;

// Recent e Preferiti sono bit: si combinano in OR (come i tipi).
enum Tag { kTagNone = 0, kTagRecent = 1, kTagFavorite = 2 };

static const uint32 kTagFilterMask = 0x03;

enum ItemFlags {
	kFlagFavorite = 1,
	kFlagRecent = 2,
	kFlagSymlink = 4,
	kFlagSystem = 8,
	kFlagPackaged = 16
};

enum StateSource { kFromUser = 0, kFromArgs = 1 };

struct FacetState {
	World	world;
	uint32	kindMask;		// 0 = tutti
	uint32	tags;			// maschera di Tag, 0 = nessuna restrizione
	BString	text;

	FacetState() : world(kWorldCatalog), kindMask(0), tags(0) {}

	bool operator==(const FacetState& o) const
	{
		return world == o.world && kindMask == o.kindMask && tags == o.tags
			&& text == o.text;
	}
};

struct ViewOptions {
	bool groupByTag;
	bool hideSystem;
	ViewOptions() : groupByTag(true), hideSystem(false) {}
};

enum StartupMode { kStartDefault = 0, kStartLast, kStartSaved };

struct ViewSpec {
	World	world;
	uint32	kindMask;
	uint32	tags;
	ViewSpec() : world(kWorldCatalog), kindMask(0), tags(0) {}
};

struct Settings {
	ViewOptions	options;
	StartupMode	startupMode;
	bool		hasSavedView;
	ViewSpec	savedView;
	ViewSpec	lastView;
	float		windowHeightUnits;	// altezza in unita' di font; 0 = predefinita
	Settings() : startupMode(kStartDefault), hasSavedView(false),
		windowHeightUnits(0) {}
};

struct NodeRefLess {
	bool operator()(const node_ref& a, const node_ref& b) const
	{
		if (a.device != b.device)
			return a.device < b.device;
		return a.node < b.node;
	}
};

#endif	// SPIELBAR_SPIELBARTYPES_H
