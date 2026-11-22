/*
 * SpielBar v3 - la finestra principale
 *
 * Generato da tools/split_app.py a partire da App.cpp (logica invariata).
 */

#include <Button.h>
#include <Catalog.h>
#include <ControlLook.h>
#include <GroupLayout.h>
#include <LayoutBuilder.h>
#include <Locale.h>
#include <Menu.h>
#include <MenuItem.h>
#include <Message.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <Rect.h>
#include <Screen.h>
#include <ScrollView.h>
#include <Size.h>
#include <String.h>
#include <StringView.h>
#include <TextControl.h>
#include <View.h>
#include <math.h>
#include <algorithm>
#include <vector>

#include "DeskbarWindow.h"
#include "SpielBarDefs.h"
#include "SpielBarTypes.h"
#include "UiMetrics.h"
#include "Formatting.h"
#include "SettingsStore.h"
#include "Emblems.h"
#include "ToolbarButtons.h"
#include "SegmentedControl.h"
#include "PlaceholderTextControl.h"
#include "ResizeHandle.h"
#include "UnifiedListView.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SpielBar"


// ---- Interfaccia -------------------------------------------------
void
DeskbarWindow::_BuildInterface()
{
	fDisksBtn = new DisksButton("disks", kIconMount);
	fGoBtn = new GoButton("go", kIconGoTo);
	fQueryBtn = new QueryButton("query", kIconQuery);
	fViewBtn = new ViewButton("view", kIconView,
		[this]() { return _BuildViewMenu(); });
	ShutdownButton* shutdownBtn = new ShutdownButton("shutdown", kIconShutdown);
	fShutdownBtn = shutdownBtn;

	fDisksBtn->SetToolTip(ToolTipWith(B_TRANSLATE("Disks"), "Cmd+D").String());
	fGoBtn->SetToolTip(ToolTipWith(B_TRANSLATE("Go"), "Cmd+G").String());
	fQueryBtn->SetToolTip(ToolTipWith(B_TRANSLATE("Search"), "Cmd+F").String());
	fViewBtn->SetToolTip(ToolTipWith(B_TRANSLATE("View"), "Cmd+,").String());
	shutdownBtn->SetToolTip(
		ToolTipWith(B_TRANSLATE("Shutdown"), "Cmd+Shift+Q").String());

	// Con i comandi in basso i menu si aprono verso l'alto.
	IconButton* buttons[] = { fDisksBtn, fGoBtn, fQueryBtn, fViewBtn,
		shutdownBtn };
	for (IconButton* button : buttons)
		button->SetMenuUp(!fControlsOnTop);

	BGroupView* buttonBar = new BGroupView(B_HORIZONTAL, 5);
	BLayoutBuilder::Group<>(buttonBar)
		.SetInsets(0)
		.Add(fDisksBtn)
		.Add(fGoBtn)
		.Add(fQueryBtn)
		.AddGlue()
		.Add(fViewBtn)
		.Add(shutdownBtn);

	// --- Campo di ricerca
	fFilter = new PlaceholderTextControl("filter",
		new BMessage(kMsgApplyFilter));
	fFilter->SetTarget(this);
	fFilter->SetModificationMessage(new BMessage(kMsgApplyFilter));

	// --- Faccette: una sola riga, sempre nello stesso posto, senza
	// etichette di testo (i nomi stanno nei tooltip):
	//   ( Apps | Preflets | Files | Folders )  ( recent | favorite )   N items
	// Fuori dal loro mondo i segmenti si disabilitano, non spariscono.
	fKindSeg = new SegmentedControl("kinds", kMsgToggleKind, "kind");
	fKindSeg->AddSegment(kKindApp, B_TRANSLATE("Apps"),
		ToolTipWith(B_TRANSLATE("Apps"), "Cmd+1").String(), kEmbKindApp);
	fKindSeg->AddSegment(kKindPreflet, B_TRANSLATE("Preflets"),
		ToolTipWith(B_TRANSLATE("Preflets"), "Cmd+2").String(),
		kEmbKindPreflet);
	fKindSeg->AddSegment(kKindFile,
		B_TRANSLATE_COMMENT("Files", "Filter chip"),
		ToolTipWith(B_TRANSLATE_COMMENT("Files", "Filter chip"),
			"Cmd+3").String(), kEmbKindFile);
	fKindSeg->AddSegment(kKindFolder, B_TRANSLATE("Folders"),
		ToolTipWith(B_TRANSLATE("Folders"), "Cmd+4").String(),
		kEmbKindFolder);
	fKindSeg->SetTarget(this);

	const char* recentLabel = B_TRANSLATE_COMMENT("Recent",
		"Filter chip and section header");
	fTagSeg = new SegmentedControl("tags", kMsgSetTag, "tag");
	fTagSeg->AddSegment(kTagRecent, recentLabel,
		ToolTipWith(recentLabel, "Cmd+J").String(), kEmbRecent);
	fTagSeg->AddSegment(kTagFavorite, B_TRANSLATE("Favorites"),
		ToolTipWith(B_TRANSLATE("Favorites"), "Cmd+S").String(),
		kEmbFavorite);
	fTagSeg->SetTarget(this);

	fCounter = new BStringView("counter", "");
	fCounter->SetAlignment(B_ALIGN_RIGHT);
	// Larghezza minima fissa: il testo cambia a ogni ricostruzione e
	// non deve far ridisporre la riga.
	fCounter->SetExplicitMinSize(BSize(
		be_plain_font->StringWidth("0000+ results") + 8, B_SIZE_UNSET));

	BGroupView* facetRow = new BGroupView(B_HORIZONTAL, 10);
	BLayoutBuilder::Group<>(facetRow)
		.SetInsets(0)
		.Add(fKindSeg)
		.Add(fTagSeg)
		.AddGlue()
		.Add(fCounter);

	// --- Lista
	fMainList = new UnifiedListView("main_list");
	fMainList->SetInvocationMessage(new BMessage(kMsgLaunch));
	fMainList->SetTarget(this);
	fMainList->SetFlags(fMainList->Flags() & ~B_NAVIGABLE);
	BScrollView* listScroll = new BScrollView("list_scroll", fMainList, 0,
		false, true);
	// Almeno kMinListRows righe sempre visibili: il minimo del layout (e
	// quindi della finestra) le include, cosi' trascinando la maniglia e
	// al riavvio vale lo stesso limite.
	listScroll->SetExplicitMinSize(BSize(B_SIZE_UNSET,
		(float)(kMinListRows * UI().rowHeight + 4)));

	// --- Layout speculare. I comandi stanno dal lato della Deskbar, la
	// lista e la maniglia dal lato opposto: il mouse fa meno strada tra
	// la barra e i pulsanti/il filtro. Deskbar in alto:
	//   pulsanti, filtro, faccette, lista, maniglia;
	// Deskbar in basso, l'ordine esatto inverso.
	std::vector<BView*> order;
	order.push_back(buttonBar);
	if (kFilterOnTop) {
		order.push_back(fFilter);
		order.push_back(facetRow);
		order.push_back(listScroll);
	} else {
		order.push_back(facetRow);
		order.push_back(listScroll);
		order.push_back(fFilter);
	}
	if (!fControlsOnTop)
		std::reverse(order.begin(), order.end());

	BGroupView* content = new BGroupView(B_VERTICAL, 6);
	if (fControlsOnTop)
		BLayoutBuilder::Group<>(content).SetInsets(10, 10, 10, 6);
	else
		BLayoutBuilder::Group<>(content).SetInsets(10, 6, 10, 10);
	BGroupLayout* contentLayout = content->GroupLayout();
	for (BView* view : order) {
		if (view == listScroll)
			contentLayout->AddView(view, 1.0f);
		else
			contentLayout->AddView(view);
	}

	// --- Maniglia: in fondo se i comandi sono in alto, in cima altrimenti.
	fResizeHandle = new ResizeHandle(fControlsOnTop,
		[this](float height, bool commit) {
			_ResizeHeightTo(height, commit);
		},
		[this]() { _ResetHeight(); });

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0).SetInsets(0);
	BGroupLayout* root = dynamic_cast<BGroupLayout*>(GetLayout());
	if (fControlsOnTop) {
		root->AddView(content, 1.0f);
		root->AddView(fResizeHandle);
	} else {
		root->AddView(fResizeHandle);
		root->AddView(content, 1.0f);
	}
}

// Menu Vista (piano par. 2.2): costruito a ogni apertura.
BPopUpMenu*
DeskbarWindow::_BuildViewMenu()
{
	BPopUpMenu* menu = new BPopUpMenu("view_menu", false, false);

	BMenuItem* group = new BMenuItem(B_TRANSLATE("Group by tag"),
		new BMessage(kMsgOptGroup));
	group->SetMarked(fSettings.options.groupByTag);
	group->SetEnabled(fState.world == kWorldCatalog);
	menu->AddItem(group);

	BMenuItem* hide = new BMenuItem(B_TRANSLATE("Hide system apps"),
		new BMessage(kMsgOptHideSystem));
	hide->SetMarked(fSettings.options.hideSystem);
	hide->SetEnabled(fState.world == kWorldCatalog
		|| fState.world == kWorldSearch);
	menu->AddItem(hide);

	menu->AddSeparatorItem();

	BMenu* startup = new BMenu(B_TRANSLATE("Startup view"));
	BMessage* m;

	m = new BMessage(kMsgStartupMode);
	m->AddInt32("mode", kStartDefault);
	BMenuItem* defItem = new BMenuItem(B_TRANSLATE("Default view"), m);
	defItem->SetMarked(fSettings.startupMode == kStartDefault);
	startup->AddItem(defItem);

	m = new BMessage(kMsgStartupMode);
	m->AddInt32("mode", kStartLast);
	BMenuItem* lastItem = new BMenuItem(B_TRANSLATE("Last used view"), m);
	lastItem->SetMarked(fSettings.startupMode == kStartLast);
	startup->AddItem(lastItem);

	m = new BMessage(kMsgStartupMode);
	m->AddInt32("mode", kStartSaved);
	BString savedLabel = B_TRANSLATE("Saved: %view%");
	savedLabel.ReplaceFirst("%view%", fSettings.hasSavedView
		? DescribeView(fSettings.savedView).String() : B_TRANSLATE("none"));
	BMenuItem* savedItem = new BMenuItem(savedLabel.String(), m);
	savedItem->SetMarked(fSettings.startupMode == kStartSaved
		&& fSettings.hasSavedView);
	savedItem->SetEnabled(fSettings.hasSavedView);
	startup->AddItem(savedItem);

	startup->AddSeparatorItem();
	startup->AddItem(new BMenuItem(B_TRANSLATE("Save current view as startup"),
		new BMessage(kMsgSaveStartupView)));

	menu->AddItem(startup);
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem(B_TRANSLATE("Deskbar preferences" B_UTF8_ELLIPSIS),
		new BMessage(kMsgDeskbarPrefs)));

	// SetTargetForItems non scende nei sottomenu: target esplicito su tutte
	// le voci, altrimenti "Startup view" non riceverebbe i clic.
	SetMenuTarget(menu, this);
	return menu;
}

bool
DeskbarWindow::_HasFacets() const
{
	return fState.world == kWorldCatalog || fState.world == kWorldSearch;
}

void
DeskbarWindow::_StoreCurrentAs(ViewSpec& spec) const
{
	spec.world = fState.world;
	spec.kindMask = fState.kindMask;
	spec.tags = fState.tags;
}

// ---- Ridimensionamento verticale ---------------------------------
// Altezza ammessa: minimo del layout, massimo lo spazio fino al bordo
// dello schermo dal lato lontano dalla Deskbar.
float
DeskbarWindow::_ClampHeight(float height, const BRect& frame)
{
	BRect screen = BScreen().Frame();
	float minW, maxW, minH, maxH;
	GetSizeLimits(&minW, &maxW, &minH, &maxH);
	// Il minimo si legge dal layout (sempre aggiornato), non dai limiti
	// della finestra, che possono essere in ritardo.
	minH = std::max(minH, GetLayout()->MinSize().height);
	float room = fControlsOnTop ? screen.bottom - frame.top
		: frame.bottom - screen.top;
	return std::max(minH, std::min(height, std::min(maxH, room)));
}

// Imposta l'altezza tenendo fermo il lato vicino alla Deskbar. Con i
// comandi in basso la finestra si allunga verso l'alto: quando cresce si
// sposta prima il bordo superiore, quando si accorcia si ridimensiona
// prima, cosi' il bordo inferiore non scende mai sulla Deskbar.
void
DeskbarWindow::_ApplyHeight(float height, const BRect& anchor, float fromHeight)
{
	float width = anchor.Width();
	if (fControlsOnTop) {
		ResizeTo(width, height);
	} else if (height > fromHeight) {
		MoveTo(anchor.left, anchor.bottom - height);
		ResizeTo(width, height);
	} else {
		ResizeTo(width, height);
		MoveTo(anchor.left, anchor.bottom - height);
	}
}

// Salvata in unita' di font: con un font piu' grande la finestra non si
// ripristina troppo piccola.
void
DeskbarWindow::_SaveHeight(float height)
{
	float units = height / UI().fontSize;
	fSettings.windowHeightUnits
		= fabsf(units - kDefaultHeightUnits) < 0.05f ? 0 : units;
	SettingsSave(fSettings);
}

// Contorno di anteprima del rettangolo finale: il bordo che si muove piu'
// i due lati, ognuno una finestra sottilissima senza focus. Si vede dove
// finira' il bordo e il rettangolo "a contorno" intero. (B_OUTLINE_RESIZE
// non si puo' pilotare da codice: vale solo per il ridimensionamento fatto
// dal decorator della finestra.)
void
DeskbarWindow::_ShowGhost(float height)
{
	float t = (float)std::max(3, (int)roundf(UI().fontSize * 0.25f));
	float s = std::max(2.0f, t - 1);

	float top = fControlsOnTop ? fDragFrame.top : fDragFrame.bottom - height;
	float bottom = fControlsOnTop ? fDragFrame.top + height : fDragFrame.bottom;
	BRect target(fDragFrame.left, top, fDragFrame.right, bottom);

	BRect frames[3];
	frames[0] = fControlsOnTop
		? BRect(target.left, target.bottom - t + 1, target.right, target.bottom)
		: BRect(target.left, target.top, target.right, target.top + t - 1);
	frames[1] = BRect(target.left, target.top, target.left + s - 1,
		target.bottom);
	frames[2] = BRect(target.right - s + 1, target.top, target.right,
		target.bottom);

	for (int i = 0; i < 3; i++) {
		if (fGhost[i] == NULL) {
			fGhost[i] = new ResizeGhost(frames[i]);
			fGhost[i]->Show();	// il primo Show() la avvia
		} else if (fGhost[i]->Lock()) {
			fGhost[i]->MoveTo(frames[i].left, frames[i].top);
			fGhost[i]->ResizeTo(frames[i].Width(), frames[i].Height());
			fGhost[i]->Unlock();
		}
	}
}

// Il contorno si distrugge a fine trascinamento: nessuna finestra
// fluttuante resta in giro tra un gesto e l'altro.
void
DeskbarWindow::_DestroyGhost()
{
	for (int i = 0; i < 3; i++) {
		if (fGhost[i] != NULL) {
			if (fGhost[i]->Lock())
				fGhost[i]->Quit();
			fGhost[i] = NULL;
		}
	}
}

// Chiamata dalla maniglia: durante il trascinamento (commit == false) con
// la nuova altezza, al rilascio (commit == true) per applicarla e
// salvarla. Il frame di partenza si memorizza una volta sola a inizio
// trascinamento: Frame() puo' non riflettere ancora i MoveTo/ResizeTo
// precedenti (gli aggiornamenti dal server sono asincroni).
// Con kLiveResize == false la finestra non si tocca finche' il mouse non
// viene rilasciato: ci si limita a muovere la linea fantasma.
void
DeskbarWindow::_ResizeHeightTo(float newHeight, bool commit)
{
	if (commit) {
		if (!fDragActive)
			return;
		fDragActive = false;
		fMainList->SetHoverEnabled(true);
		if (!kLiveResize) {
			_DestroyGhost();
			if (fabsf(fPendingHeight - fDragHeight) >= 0.5f) {
				_ApplyHeight(fPendingHeight, fDragFrame, fDragHeight);
				fDragHeight = fPendingHeight;
			}
		}
		_SaveHeight(fDragHeight);
		return;
	}

	if (!fDragActive) {
		fDragFrame = Frame();
		fDragHeight = fDragFrame.Height();
		fPendingHeight = fDragHeight;
		fDragActive = true;
		// Il puntatore attraversa la lista: niente evidenziazione al passaggio.
		fMainList->SetHoverEnabled(false);
	}

	float height = _ClampHeight(newHeight, fDragFrame);
	if (kLiveResize) {
		if (fabsf(height - fDragHeight) < 0.5f)
			return;
		_ApplyHeight(height, fDragFrame, fDragHeight);
		fDragHeight = height;
	} else {
		fPendingHeight = height;
		_ShowGhost(height);
	}
}

// Doppio clic sulla maniglia: altezza predefinita, subito e senza fantasma.
void
DeskbarWindow::_ResetHeight()
{
	BRect frame = Frame();
	float height = _ClampHeight(roundf(UI().fontSize * kDefaultHeightUnits),
		frame);
	if (fabsf(height - frame.Height()) >= 0.5f)
		_ApplyHeight(height, frame, frame.Height());
	_SaveHeight(height);
}
