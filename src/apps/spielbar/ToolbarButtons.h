/*
 * SpielBar v3 - pulsanti della barra superiore
 *
 * Generato da tools/split_app.py a partire da App.cpp (logica invariata).
 */

#ifndef SPIELBAR_TOOLBARBUTTONS_H
#define SPIELBAR_TOOLBARBUTTONS_H

#include <Bitmap.h>
#include <Button.h>
#include <Catalog.h>
#include <Entry.h>
#include <FindDirectory.h>
#include <Locale.h>
#include <Menu.h>
#include <MenuItem.h>
#include <Message.h>
#include <Path.h>
#include <Point.h>
#include <PopUpMenu.h>
#include <Roster.h>
#include <Size.h>
#include <SupportDefs.h>
#include <TypeConstants.h>
#include <Window.h>
#include <stdio.h>
#include <functional>

#include "SpielBarDefs.h"
#include "SpielBarTypes.h"
#include "UiMetrics.h"
#include "IconSupport.h"
#include "Emblems.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SpielBar"


class IconButton : public BButton {
public:
	IconButton(const char* name, int32 resID, BMessage* msg)
		: BButton(name, NULL, msg)
		, fTracking(false)
		, fWasOn(false)
		, fMenuUp(false)
	{
		const UiMetrics& ui = UI();
		BBitmap* icon = LoadVectorIcon(resID, ui.buttonIcon);
		if (icon == NULL) {
			fprintf(stderr, "SpielBar: icona HVIF %d non trovata nel .rdef\n",
				(int)resID);
		} else {
			SetIcon(icon);
			delete icon;	// SetIcon copia la bitmap
		}
		BSize size((float)ui.buttonSize, (float)ui.buttonSize);
		SetExplicitMinSize(size);
		SetExplicitMaxSize(size);
		SetExplicitPreferredSize(size);
		SetFlags(Flags() & ~B_NAVIGABLE);
	}

	// I pulsanti in basso aprono i menu verso l'alto.
	void SetMenuUp(bool up) { fMenuUp = up; }

	// Toggle = pulsante "mondo" che resta premuto finche' il mondo e' attivo.
	virtual bool IsToggle() const { return false; }

	// true: il menu si apre in modo sincrono (serve per sapere quando si chiude).
	virtual bool MenuIsSynchronous() const { return false; }

	virtual void InvokePrimaryAction() {}

	virtual BPopUpMenu* BuildContextMenu() { return nullptr; }

	// Menu nel punto indicato (clic destro: dove sta il puntatore).
	void InvokeContextMenu(BPoint screenWhere)
	{
		BPopUpMenu* menu = BuildContextMenu();
		if (menu != NULL)
			_Go(menu, screenWhere);
	}

	// Menu ancorato al pulsante, sotto o sopra secondo la posizione della
	// barra dei comandi (tastiera, clic sinistro sul pulsante View).
	void InvokeMenuAtButton(bool alignRight)
	{
		BPopUpMenu* menu = BuildContextMenu();
		if (menu == NULL)
			return;

		BSize size = menu->PreferredSize();
		float width = size.width < 50 ? 0 : size.width;
		float height = size.height < 20 ? 0 : size.height;
		BPoint where(alignRight ? Bounds().right - width + 1 : Bounds().left,
			fMenuUp ? Bounds().top - height - 2 : Bounds().bottom + 2);
		ConvertToScreen(&where);
		_Go(menu, where);
	}

	void MouseDown(BPoint where) override
	{
		uint32 buttons = 0;
		GetMouse(&where, &buttons, false);

		if (buttons & B_SECONDARY_MOUSE_BUTTON) {
			ConvertToScreen(&where);
			InvokeContextMenu(where);
		} else if (buttons & B_PRIMARY_MOUSE_BUTTON) {
			fTracking = true;
			fWasOn = (Value() == B_CONTROL_ON);
			if (!fWasOn)
				SetValue(B_CONTROL_ON);
			SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
		}
	}

	void MouseUp(BPoint where) override
	{
		if (!fTracking)
			return;
		fTracking = false;

		bool inside = Bounds().Contains(where);
		if (inside)
			InvokePrimaryAction();

		if (!IsToggle()) {
			SetValue(B_CONTROL_OFF);
		} else if (!inside && !fWasOn) {
			SetValue(B_CONTROL_OFF);
		}
		// Toggle dentro: il valore lo riallinea la finestra (_SyncControls).
	}

protected:
	void _PostToWindow(BMessage* msg)
	{
		if (Window() != NULL)
			Window()->PostMessage(msg);
	}

private:
	void _Go(BPopUpMenu* menu, BPoint where)
	{
		SetMenuTarget(menu, Window());
		if (MenuIsSynchronous()) {
			menu->Go(where, true, false, false);
			delete menu;
		} else {
			// Asincrono: il popup si autodistrugge. Niente delete.
			menu->Go(where, true, false, true);
		}
	}

	bool fTracking;
	bool fWasOn;
	bool fMenuUp;
};

class DisksButton : public IconButton {
public:
	DisksButton(const char* name, int32 resID)
		: IconButton(name, resID, new BMessage(kMsgToggleWorld)) {}

	bool IsToggle() const override { return true; }

	void InvokePrimaryAction() override
	{
		BMessage msg(kMsgToggleWorld);
		msg.AddInt32("world", kWorldDisks);
		_PostToWindow(&msg);
	}

	BPopUpMenu* BuildContextMenu() override
	{
		BPopUpMenu* menu = new BPopUpMenu("Disks Menu", false, false);
		menu->AddItem(new BMenuItem(B_TRANSLATE("Mount all"),
			new BMessage(kMountAllNow)));
		menu->AddItem(new BMenuItem(B_TRANSLATE("Unmount all"),
			new BMessage(kUnmountAll)));

		menu->AddSeparatorItem();

		menu->AddItem(new BMenuItem(B_TRANSLATE("Settings" B_UTF8_ELLIPSIS),
			new BMessage(kRunAutomounterSettings)));
		return menu;
	}
};

class GoButton : public IconButton {
public:
	GoButton(const char* name, int32 resID)
		: IconButton(name, resID, new BMessage(kMsgToggleWorld)) {}

	bool IsToggle() const override { return true; }

	void InvokePrimaryAction() override
	{
		BMessage msg(kMsgToggleWorld);
		msg.AddInt32("world", kWorldGo);
		_PostToWindow(&msg);
	}

	BPopUpMenu* BuildContextMenu() override
	{
		BPopUpMenu* menu = new BPopUpMenu("Go Menu", false, false);
		BPath goPath;
		if (find_directory(B_USER_SETTINGS_DIRECTORY, &goPath) == B_OK) {
			goPath.Append("Tracker/Go");
			BMessage* msg = new BMessage(kMsgOpenFolder);
			msg->AddString("path", goPath.Path());
			menu->AddItem(new BMenuItem(
				B_TRANSLATE("Open Go folder" B_UTF8_ELLIPSIS), msg));
		}
		return menu;
	}
};

class QueryButton : public IconButton {
public:
	QueryButton(const char* name, int32 resID)
		: IconButton(name, resID, new BMessage(kMsgToggleWorld)) {}

	bool IsToggle() const override { return true; }

	void InvokePrimaryAction() override
	{
		BMessage msg(kMsgToggleWorld);
		msg.AddInt32("world", kWorldSearch);
		_PostToWindow(&msg);
	}

	BPopUpMenu* BuildContextMenu() override
	{
		BPopUpMenu* menu = new BPopUpMenu("Query Menu", false, false);

		menu->AddItem(new BMenuItem(
			B_TRANSLATE("Open Query window" B_UTF8_ELLIPSIS),
			new BMessage(kMsgOpenQueryWindow)));

		menu->AddSeparatorItem();

		// Query recenti in un sottomenu, disabilitato se vuoto.
		BMenu* recent = new BMenu(B_TRANSLATE("Recent queries" B_UTF8_ELLIPSIS));
		_FillRecentQueries(recent);
		BMenuItem* recentItem = new BMenuItem(recent);
		recentItem->SetEnabled(recent->CountItems() > 0);
		menu->AddItem(recentItem);

		menu->AddSeparatorItem();

		BPath queryDirPath;
		find_directory(B_USER_DIRECTORY, &queryDirPath);
		queryDirPath.Append("queries");
		BMessage* openFolderMsg = new BMessage(kMsgOpenFolder);
		openFolderMsg->AddString("path", queryDirPath.Path());
		menu->AddItem(new BMenuItem(B_TRANSLATE("Open Queries Folder"),
			openFolderMsg));

		return menu;
	}

private:
	void _FillRecentQueries(BMenu* menu)
	{
		BMessage recentDocs;
		be_roster->GetRecentDocuments(&recentDocs, 10,
			"application/x-vnd.Be-query");

		type_code type;
		int32 count;
		if (recentDocs.GetInfo("refs", &type, &count) != B_OK)
			return;
		for (int32 i = 0; i < count; i++) {
			entry_ref ref;
			if (recentDocs.FindRef("refs", i, &ref) != B_OK)
				continue;
			BEntry entry(&ref);
			if (!entry.Exists())
				continue;
			BMessage* msg = new BMessage(kOpenQuery);
			msg->AddRef("refs", &ref);
			menu->AddItem(new BMenuItem(ref.name, msg));
		}
	}
};

// Menu Vista: lo costruisce la finestra a ogni apertura, cosi' le spunte
// riflettono lo stato reale.
class ViewButton : public IconButton {
public:
	ViewButton(const char* name, int32 resID,
			std::function<BPopUpMenu*()> builder)
		: IconButton(name, resID, NULL)
		, fBuilder(builder) {}

	bool MenuIsSynchronous() const override { return true; }

	BPopUpMenu* BuildContextMenu() override
	{
		return fBuilder ? fBuilder() : NULL;
	}

	// Allineato al bordo destro del pulsante.
	void InvokePrimaryAction() override
	{
		InvokeMenuAtButton(true);
	}

private:
	std::function<BPopUpMenu*()> fBuilder;
};

class ShutdownButton : public IconButton {
public:
	ShutdownButton(const char* name, int32 resID)
		: IconButton(name, resID, NULL) {}

	void InvokePrimaryAction() override
	{
		_PostToWindow(new BMessage(kMsgAskHalt));
	}

	BPopUpMenu* BuildContextMenu() override
	{
		BPopUpMenu* menu = new BPopUpMenu("Shutdown Menu", false, false);
		menu->AddItem(new BMenuItem(B_TRANSLATE("Power Off"),
			new BMessage(kMsgShutDown)));
		menu->AddItem(new BMenuItem(B_TRANSLATE("Restart System"),
			new BMessage(kMsgReboot)));
		return menu;
	}
};

#endif	// SPIELBAR_TOOLBARBUTTONS_H
