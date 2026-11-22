/*
 * SpielBar v3 - la finestra principale
 *
 * Generato da tools/split_app.py a partire da App.cpp (logica invariata).
 */

#ifndef SPIELBAR_DESKBARWINDOW_H
#define SPIELBAR_DESKBARWINDOW_H

#include <Entry.h>
#include <Handler.h>
#include <List.h>
#include <ListItem.h>
#include <Message.h>
#include <MessageRunner.h>
#include <Node.h>
#include <Path.h>
#include <Point.h>
#include <PopUpMenu.h>
#include <Rect.h>
#include <String.h>
#include <StringView.h>
#include <SupportDefs.h>
#include <Window.h>
#include <atomic>
#include <map>
#include <memory>
#include <vector>

#include "SpielBarTypes.h"
#include "ToolbarButtons.h"
#include "SegmentedControl.h"
#include "PlaceholderTextControl.h"
#include "ResizeHandle.h"
#include "ListItems.h"
#include "UnifiedListView.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SpielBar"


class DeskbarWindow : public BWindow {
public:
	DeskbarWindow(const FacetState* startOverride);

	~DeskbarWindow();

	void WindowActivated(bool active) override;

	void MessageReceived(BMessage* msg) override;

	void DispatchMessage(BMessage* message, BHandler* target) override;

private:
	// ---- Membri ----------------------------------------------------
	typedef std::map<node_ref, UnifiedListItem*, NodeRefLess> ItemCache;

	FacetState				fState;
	Settings				fSettings;
	ItemCache				fCache;				// possiede gli item del Catalogo
	std::map<node_ref, BString, NodeRefLess> fFavorites;	// target -> symlink
	std::map<node_ref, int32, NodeRefLess> fRecents;		// target -> posizione
	std::vector<BListItem*>	fTransient;			// intestazioni, Go, risultati
	std::vector<BListItem*>	fVolumesCache;		// item dei Dischi

	UnifiedListView*		fMainList;
	PlaceholderTextControl*	fFilter;
	BStringView*			fCounter;
	SegmentedControl*		fKindSeg;
	SegmentedControl*		fTagSeg;
	IconButton*				fDisksBtn;
	IconButton*				fGoBtn;
	IconButton*				fQueryBtn;
	ViewButton*				fViewBtn;
	IconButton*				fShutdownBtn;

	BMessageRunner*			fCloseRunner;
	BMessageRunner*			fDebounceRunner;
	// Il lancio con Shift ha preso il focus: la prossima disattivazione non
	// deve chiudere la finestra (si azzera subito dopo, vale una volta sola).
	bool					fKeepOpenAfterLaunch;
	static constexpr bigtime_t kDebounceTime = 250000;

	std::shared_ptr<std::atomic<uint32> > fSearchGen;
	int32					fSearchCount;
	bool					fSearchTruncated;
	bool					fSearchDone;
	bool					fUserMovedSelection;
	bool					fControlsOnTop;	// Deskbar in alto: comandi in alto
	ResizeHandle*			fResizeHandle;
	bool					fDragActive;	// trascinamento della maniglia in corso
	BRect					fDragFrame;		// frame a inizio trascinamento
	float					fDragHeight;	// ultima altezza impostata
	float					fPendingHeight;	// altezza scelta, da applicare al rilascio
	ResizeGhost*			fGhost[3];		// contorno: bordo mobile + due lati

	// ---- Interfaccia -------------------------------------------------
	void _BuildInterface();

	// Menu Vista (piano par. 2.2): costruito a ogni apertura.
	BPopUpMenu* _BuildViewMenu();

	bool _HasFacets() const;

	void _StoreCurrentAs(ViewSpec& spec) const;

	// ---- Ridimensionamento verticale ---------------------------------
	// Altezza ammessa: minimo del layout, massimo lo spazio fino al bordo
	// dello schermo dal lato lontano dalla Deskbar.
	float _ClampHeight(float height, const BRect& frame);

	// Imposta l'altezza tenendo fermo il lato vicino alla Deskbar. Con i
	// comandi in basso la finestra si allunga verso l'alto: quando cresce si
	// sposta prima il bordo superiore, quando si accorcia si ridimensiona
	// prima, cosi' il bordo inferiore non scende mai sulla Deskbar.
	void _ApplyHeight(float height, const BRect& anchor, float fromHeight);

	// Salvata in unita' di font: con un font piu' grande la finestra non si
	// ripristina troppo piccola.
	void _SaveHeight(float height);

	// Contorno di anteprima del rettangolo finale: il bordo che si muove piu'
	// i due lati, ognuno una finestra sottilissima senza focus. Si vede dove
	// finira' il bordo e il rettangolo "a contorno" intero. (B_OUTLINE_RESIZE
	// non si puo' pilotare da codice: vale solo per il ridimensionamento fatto
	// dal decorator della finestra.)
	void _ShowGhost(float height);

	// Il contorno si distrugge a fine trascinamento: nessuna finestra
	// fluttuante resta in giro tra un gesto e l'altro.
	void _DestroyGhost();

	// Chiamata dalla maniglia: durante il trascinamento (commit == false) con
	// la nuova altezza, al rilascio (commit == true) per applicarla e
	// salvarla. Il frame di partenza si memorizza una volta sola a inizio
	// trascinamento: Frame() puo' non riflettere ancora i MoveTo/ResizeTo
	// precedenti (gli aggiornamenti dal server sono asincroni).
	// Con kLiveResize == false la finestra non si tocca finche' il mouse non
	// viene rilasciato: ci si limita a muovere la linea fantasma.
	void _ResizeHeightTo(float newHeight, bool commit);

	// Doppio clic sulla maniglia: altezza predefinita, subito e senza fantasma.
	void _ResetHeight();

	// ---- Stato -------------------------------------------------------
	// Piano par. 5.2.
	void _SetState(FacetState wanted, int32 source, bool force = false);

	void _OnWorldChanged(World oldWorld);

	// Allinea pulsanti e segmenti allo stato, senza messaggi di ritorno.
	// I segmenti fuori dal loro mondo restano visibili ma disabilitati:
	// la riga non cambia mai posto ne' altezza.
	void _SyncControls();

	void _UpdateCounter(int32 count, int mode);

	void _ClearCounter();

	// Raggruppamento in sezioni: solo nel Catalogo, con l'opzione attiva e
	// senza restrizione di tag (o con entrambi i tag, che in OR lasciano
	// solo le sezioni Preferiti e Recenti).
	bool _IsGrouped() const;

	// ---- Proprieta' degli item (piano par. 3.5) ------------------------
	void _FreeTransient();

	void _ClearList();

	// ---- Indici e cache ----------------------------------------------
	UnifiedListItem* _CacheEnsure(const entry_ref& ref, bool prefletHint,
			bool detectSymlink);

	void _ScanCatalog();

	void _ScanDirectory(const char* dirPath, bool prefs, int depth);

	bool _FavoritesPath(BPath* path);

	void _RefreshFavorites();

	void _RefreshRecents();

	uint32 _IndexFlagsFor(const node_ref& node) const;

	void _ApplyIndexFlags(UnifiedListItem* item);

	// Aggiorna i flag Favorite/Recent di tutti gli item vivi.
	void _RefreshFlags();

	int32 _RecentPosition(const UnifiedListItem* item) const;

	// Piano par. 3.4.
	void _ToggleFavorite(const entry_ref& ref);

	void _SelectNode(const node_ref& node);

	void _AddToGoTo(const entry_ref& ref);

	void _CreateSymlink(const entry_ref& ref, const char* targetDir);

	// ---- Volumi ---------------------------------------------------------
	void _RefreshVolumes();

	void _LoadVolumes(BList* outList);

	// ---- Ricostruzione della lista (piano par. 5.4) ----------------------
	void _Rebuild();

	void _AddSection(const char* label, std::vector<UnifiedListItem*>& items);

	void _RebuildCatalog();

	void _RebuildDisks();

	void _RebuildGo();

	// Elenca i symlink di una cartella (Go) creando item transitori.
	void _ScanLinkDirectory(const char* dirPath,
			std::vector<UnifiedListItem*>& out);

	void _SelectFirst();

	// ---- Cerca (piano par. 6) ---------------------------------------------
	void _ScheduleSearch();

	void _StartSearch();

	void _HandleSearchResults(BMessage* msg);

	void _UpdateSearchCounter();

	// ---- Selezione e tastiera ---------------------------------------------
	int32 _NextSelectable(int32 from, int32 direction);

	void _SelectIndex(int32 index, bool byUser);

	UnifiedListItem* _SelectedItem();

	bool _HandleListKey(BMessage* message, char key);

	// Esc: cancella il testo; altrimenti torna al Catalogo; altrimenti
	// azzera le faccette.
	void _HandleEscape();

	void _ToggleWorldKey(World world);

	// Menu del pulsante del mondo attivo (Shift+F10): Dischi, Go o Cerca.
	void _OpenWorldMenu();

	bool _HandleCommandKey(char key, int32 modifiers);

	// ---- Lancio e menu contestuale ----------------------------------------
	// keepOpen (Shift): lancia senza chiudere la finestra, cosi' se ne
	// possono lanciare piu' d'uno di seguito.
	void _LaunchSelectedItem(bool keepOpen = false);

	// Chiude o resta aperta a seconda di keepOpen: unico punto in cui il
	// lancio decide il destino della finestra.
	void _FinishLaunch(bool keepOpen);

	void _ShowContextMenu(BPoint where, int32 index, bool keepOpen = false);

	void _ShowVolumeMenu(BPoint where, VolumeListItem* volItem);

	// ---- Worker di mount/unmount (invariati, salvo il messaggio di refresh) --
	static int32 _MountAllWorker(void* data);

	static int32 _MountWorker(void* data);

	static int32 _UnmountAllWorker(void* data);
};

#endif	// SPIELBAR_DESKBARWINDOW_H
