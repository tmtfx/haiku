/*
 * SpielBar v3 - costanti: messaggi, id delle icone, misure di base
 *
 * Generato da tools/split_app.py a partire da App.cpp (logica invariata).
 */

#ifndef SPIELBAR_SPIELBARDEFS_H
#define SPIELBAR_SPIELBARDEFS_H

#include <SupportDefs.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SpielBar"


const uint32 kRaiseDeskbarBtn = 'RZBT';

const uint32 kPressDeskbarBtn = 'PRBT';

static const bool kFilterOnTop = true;

// true: la finestra segue il mouse mentre si trascina la maniglia (live);
// false: durante il trascinamento si vede solo una linea fantasma nel punto
// in cui finira' il bordo, e la finestra si ridimensiona una volta sola al
// rilascio (niente layout che balla, niente bordo che scivola).
static const bool kLiveResize = false;

// true: la finestra non si puo' spostare dall'utente (come il menu della
// Deskbar). Il posizionamento da codice (MoveTo) resta possibile; se con
// questo flag la finestra non comparisse, rimettere false.
static const bool kNotMovable = true;

// true: stampa su stderr attivazioni della finestra e calcoli di larghezza.
static const bool kDebugLayout = false;

// Altezza predefinita della finestra in "unita' di font" (560 px a 12 pt).
static const float kDefaultHeightUnits = 560.0f / 12.0f;

// Larghezza predefinita in "unita' di font" (400 px a 12 pt). Il layout la
// allarga da solo se il contenuto non ci sta.
static const float kDefaultWidthUnits = 400.0f / 12.0f;

// Righe della lista sempre visibili, anche con la finestra al minimo.
static const int kMinListRows = 3;

enum {
	kMsgShutDown = '_SHD',
	kMsgReboot = '_RBT',
	kMsgAskHalt = 'AHLT',

	kMsgGetInfo = '_NFO',
	kMsgRemoveItem = '_RMI',
	kMsgAddGoTo = '_AGT',
	kMsgRevealInTracker = '_RTR',
	kMsgOpenMenu = '_MEN',
	kMsgLaunch = '_LAU',

	kMsgDeskbarPrefs = '_PRE',
	kMsgApplyFilter = '_AFI',
	kMsgDebouncedFilter = '_DFI',
	kMsgDelayedClose = '_DCL',

	// Stato a faccette
	kMsgSetState = '_SST',
	kMsgToggleWorld = '_TWD',
	kMsgToggleKind = '_TKD',
	kMsgSetTag = '_STG',
	kMsgToggleFavorite = '_TFV',

	// Menu Vista
	kMsgOptGroup = '_OGR',
	kMsgOptHideSystem = '_OHS',
	kMsgStartupMode = '_SMD',
	kMsgSaveStartupView = '_SSV',

	// Query / cartelle
	kOpenQuery = 'OpQu',
	kMsgOpenQueryWindow = 'OpQW',
	kMsgOpenFolder = 'OpFo',

	// Cerca
	kMsgSearchResults = 'SeRe',
	kMsgSearchDone = 'SeDo',

	// Dischi
	kMsgRefreshVolumes = 'RfVo',
	kRunAutomounterSettings = '_AMS',
	kToggleVolume = 'ToVo',
	kMountVolume = 'MoVo',
	kUnmountVolume = 'UMVo',
	kMountAllNow = 'MoAl',
	kUnmountAll = 'UMAl',

	kRaiseQuit = 'RQTM'
};

enum {
	kIconShutdown = 1000,
	kIconMount = 1010,
	kIconQuery = 1020,
	kIconGoTo = 1030,
	kIconView = 1040,

	kIconEmblemFavorite = 1200,
	kIconEmblemFavoriteEmpty = 1210,
	kIconEmblemRecent = 1220,
	kIconEmblemSystem = 1230,
	kIconEmblemSymlink = 1240,
	kIconEmblemPackage = 1250,

	// Faccette dei tipi (opzionali: se mancano il segmento mostra il testo)
	kIconKindApp = 1100,
	kIconKindPreflet = 1110,
	kIconKindFile = 1120,
	kIconKindFolder = 1130
};

#endif	// SPIELBAR_SPIELBARDEFS_H
