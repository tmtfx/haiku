/*
 * Copyright 2026, I Pirati Del Frico
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include <AppFileInfo.h>
#include <Application.h>
#include <Bitmap.h>
#include <Button.h>
#include <Catalog.h>
#include <Deskbar.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <GroupLayout.h>
#include <IconUtils.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <LocaleRoster.h>
#include <MenuItem.h>
#include <Message.h>
#include <Messenger.h>
#include <MessageRunner.h>
#include <MimeType.h>
#include <Node.h>
#include <NodeInfo.h>
#include <Path.h>
#include <Point.h>
#include <PopUpMenu.h>
#include <Rect.h>
#include <Roster.h>
#include <Screen.h>
#include <ScrollView.h>
#include <StringItem.h>
#include <TextControl.h>
#include <View.h>
#include <Window.h>
#include <Query.h>

#include <DiskDevice.h>
#include <DiskDeviceList.h>
#include <VolumeRoster.h>
#include <Volume.h>

#include <RosterPrivate.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SpielBar"
#include <Resources.h>

#include <stdio.h>

#include <cmath>
#include <vector>
#include <cstring>
#include <algorithm>
#include <cctype>
#include <atomic>

const uint32 kRaiseDeskbarBtn				= 'RZBT';
const uint32 kPressDeskbarBtn				= 'PRBT';

enum {
	kMsgQuery    = '_QRY',
	kMsgTrash    = '_TRA',
	kMsgAbout    = '_ABT',
	kMsgShutDown = '_SHD',
	
	kMsgGetInfo = '_NFO',
	kMsgAddFavorite = '_AFV',
	kMsgRemoveItem = '_RMI',
	kMsgAddGoTo = '_AGT',
	
	kMsgPrefs = '_PRE',
	kMsgReboot = '_RBT',
	kMsgAskHalt = 'AHLT',
	kMsgRevealInTracker = '_RTR',
	kMsgEmptyTrash = '_ETR',
	
	kMsgOpenMenu = '_MEN',
	kMsgLaunch = '_LAU',
	kMsgApplyFilter = '_AFI',
	kMsgDeleteFilter = '_DFI',
	kMsgDelayedClose = '_DCL',
	
	kOpenQuery = 'OpQu',
	kOpenQueriesFolder = 'OpQF',
	kFindResults = 'FiRe',
	kFindDone    = 'FiDo',
	
	kShowApps = 'ShAp',
	kShowRecentApps = 'ShRA',
	kShowRecentDocs = 'ShRD',
	kShowRecentFolders = 'ShRF',
	kShowPrefs = 'ShPr',
	kShowFavorites = 'ShFa',
	kShowFind = 'ShFi',
	kShowGoTo = 'ShGT',
	kShowVolumes = 'ShVo',
	
	kRunAutomounterSettings = '_AMS',
	kToggleVolume = 'ToVo',
	kMountVolume = 'MoVo',
	kUnmountVolume = 'UMVo',
	kMountAllNow = 'MoAl',
	kUnmountAll = 'UMAl',
	kRaiseBtn = 'RBTN'
};

enum {
	kIconShutdown = 1000,
	kIconMount = 1010,
	kIconQuery = 1020,
	kIconTrash = 1030,
	kIconAbout = 1040,
	kIconApps = 1050,
	kIconRecentApps = 1060,
	kIconRecentDocs = 1070,
	kIconRecentFolders = 1080,
	kIconFavorites = 1090,
	kIconGoTo = 1100,
	kIconFind = 1110,
	kIconPrefs = 1120,
	kIconPreferences = 1130
};

// -------------------------------------------------------------
// Helpers
// -------------------------------------------------------------

static void OpenInfo(const char* path) {
	BEntry entry(path, true);
	if (entry.InitCheck() != B_OK)
		return;

	entry_ref ref;
	if (entry.GetRef(&ref) != B_OK)
		return;

	BMessage message('Tinf');
	message.AddRef("refs", &ref);

	BMessenger tracker("application/x-vnd.Be-TRAK");
	if (tracker.IsValid())
		tracker.SendMessage(&message);
}

static BString BuildCaseInsensitivePattern(const BString& text, bool contains) {
	BString pattern;
	if (contains)
		pattern << "*";

	for (int i = 0; i < text.Length(); i++) {
		char c = text[i];
		if (isalpha(c)) {
			pattern << "[" << (char)tolower(c) << (char)toupper(c) << "]";
		} else {
			pattern << c;
		}
	}

	pattern << "*";

	return pattern;
}

static void RevealInTracker(const entry_ref& ref) {
	BEntry entry(&ref, true);
	if (!entry.Exists())
		return;

	node_ref node;
	if (entry.GetNodeRef(&node) != B_OK)
		return;

	BEntry parent;
	if (entry.GetParent(&parent) != B_OK)
		return;
	
	entry_ref parentRef;
	if (parent.GetRef(&parentRef) != B_OK)
		return;

	// Ask Tracker to open the containing folder and select
	// the file.
	BMessenger trackerMessenger("application/x-vnd.Be-TRAK");
	if (trackerMessenger.IsValid()) {
		BMessage message(B_REFS_RECEIVED);
		message.AddRef("refs", &parentRef);
		message.AddData("nodeRefToSelect", B_RAW_TYPE, &node, sizeof(node_ref));
		trackerMessenger.SendMessage(&message);
	}
}

static BBitmap* LoadVectorIcon(int32 resourceID, float size = 32) {
	app_info appInfo;
	if (be_app->GetAppInfo(&appInfo) != B_OK)
		return NULL;

	BFile file(&appInfo.ref, B_READ_ONLY);
	if (file.InitCheck() != B_OK)
		return NULL;

	BResources res;
	if (res.SetTo(&file) != B_OK)
		return NULL;

	size_t len;
	const void* data = res.LoadResource(B_VECTOR_ICON_TYPE, resourceID, &len);
	if (!data) return NULL;

	BBitmap* bmp = new BBitmap(BRect(0, 0, size - 1, size - 1), B_RGBA32);
	if (BIconUtils::GetVectorIcon((const uint8*)data, len, bmp) != B_OK) {
		delete bmp;
		return NULL;
	}
	return bmp;
}

static BBitmap* MakeIconBitmap(int32 size) {
	return new BBitmap(BRect(0, 0, size - 1, size - 1), B_RGBA32);
}

static icon_size IconSizeFor(int32 size) {
	return (size <= 16) ? B_MINI_ICON : B_LARGE_ICON;
}

static bool TryGetIcon(BMimeType& mt, BBitmap* out, int32 size) {
	if (!mt.IsInstalled())
		return false;

	BBitmap* tmp = MakeIconBitmap(size);
	bool ok = false;
	if (mt.GetIcon(tmp, IconSizeFor(size)) == B_OK) {
		memcpy(out->Bits(), tmp->Bits(), tmp->BitsLength());
		ok = true;
	}
	delete tmp;
	return ok;
}

static BBitmap* LoadBestIconForRef(const entry_ref& ref, const char* signatureOpt, int32 size) {
	BBitmap* outIcon = MakeIconBitmap(size);
	bool loaded = false;

	{
		BNode node(&ref);
		if (node.InitCheck() == B_OK) {
			if (BIconUtils::GetVectorIcon(&node, "BEOS:ICON", outIcon) == B_OK) {
				loaded = true;
			}
		}
	}

	if (!loaded) {
		char mimeType[B_MIME_TYPE_LENGTH] = {0};
		if (signatureOpt && signatureOpt[0]) {
			strcpy(mimeType, signatureOpt);
		} else {
			BNode node(&ref);
			BNodeInfo nodeInfo(&node);
			nodeInfo.GetType(mimeType);
		}

		if (mimeType[0] != '\0') {
			BMimeType mt(mimeType);
			if (TryGetIcon(mt, outIcon, size)) {
				loaded = true;
			} else {
				BMimeType super;
				if (mt.GetSupertype(&super) == B_OK) {
					if (TryGetIcon(super, outIcon, size)) {
						loaded = true;
					}
				}
			}
		} else {
			BEntry entry(&ref, true);
			if (entry.InitCheck() == B_OK) {
				if (entry.IsDirectory()) {
					BMimeType folderType("application/x-vnd.Be-directory");
					if (TryGetIcon(folderType, outIcon, size))
						loaded = true;
				} else {
					BMimeType fileType("application/octet-stream");
					if (TryGetIcon(fileType, outIcon, size))
						loaded = true;
				}
			}
		}
	}

	if (!loaded) {
		BMimeType generic("application/octet-stream");
		if (TryGetIcon(generic, outIcon, size))
			loaded = true;
	}

	if (!loaded) {
		delete outIcon;
		return nullptr;
	}
	return outIcon;
}

static BRect GetDeskbarFrame() 
{
	BRect frame(0, 0, 0, 0);
	BMessenger messenger("application/x-vnd.Be-TSKB"); // Deskbar signature
	
	if (messenger.IsValid()) {
		BMessage reply;
		BMessage request(B_GET_PROPERTY);
		request.AddSpecifier("Frame");
		request.AddSpecifier("Window", (int32)0); // Main Deskbar window
		
		if (messenger.SendMessage(&request, &reply) == B_OK) {
			reply.FindRect("result", &frame);
		}
	}
	return frame;
}

static BPoint CalculateAppPosition(BRect windowRect)
{
	BRect dbFrame = GetDeskbarFrame();
	BDeskbar deskbar;
	int32 location = deskbar.Location();
	
	BPoint targetPoint(100, 100); // fallback

	switch (location) {
		case B_DESKBAR_TOP:
			// Deskbar in alto: il Leaf è a sinistra. 
			// Posiziona l'app sotto la Deskbar, allineata a sinistra.
			targetPoint.x = dbFrame.left + 5;
			targetPoint.y = dbFrame.bottom + 5;
			break;
			
		case B_DESKBAR_BOTTOM:
			// Deskbar in basso: il Leaf è a sinistra.
			// Posiziona l'app sopra la Deskbar.
			targetPoint.x = dbFrame.left + 5;
			targetPoint.y = dbFrame.top - windowRect.Height() - 5;
			break;
			
		case B_DESKBAR_LEFT_TOP:
		case B_DESKBAR_LEFT_BOTTOM:
			// Deskbar verticale a sinistra (stile classico). Il Leaf è in alto a sinistra.
			// Posiziona l'app a destra della Deskbar.
			targetPoint.x = dbFrame.right + 5;
			targetPoint.y = dbFrame.top + 5;
			break;
			
		case B_DESKBAR_RIGHT_TOP:
		case B_DESKBAR_RIGHT_BOTTOM:
			// Deskbar verticale a destra. Il Leaf è in alto a destra della Deskbar.
			// Posiziona l'app a sinistra della Deskbar.
			targetPoint.x = dbFrame.left - windowRect.Width() - 5;
			targetPoint.y = dbFrame.top + 5;
			break;
			
		default:
			// Altre modalità (es. espansa lungo i lati)
			targetPoint.x = dbFrame.left;
			targetPoint.y = dbFrame.bottom;
			break;
	}

	// Controllo di sicurezza: evita che la finestra esca dallo schermo
	BScreen screen;
	BRect screenFrame = screen.Frame();
	if (targetPoint.x + windowRect.Width() > screenFrame.right)
		targetPoint.x = screenFrame.right - windowRect.Width() - 5;
	if (targetPoint.y + windowRect.Height() > screenFrame.bottom)
		targetPoint.y = screenFrame.bottom - windowRect.Height() - 5;
		
	return targetPoint;
}


class IconButton : public BButton {
public:
	IconButton(const char* name, int32 resID, BMessage* msg)
		: BButton(name, NULL, msg)
	{
		if (BBitmap* icon = LoadVectorIcon(resID, 32))
			SetIcon(icon);
	}

	virtual void InvokePrimaryAction() {
		Invoke();
	}

	virtual BPopUpMenu* BuildContextMenu() {
		return nullptr;
	}

	void InvokeContextMenu() {
		BRect bounds = Bounds();
		BPoint where(bounds.left, bounds.bottom + 2);
		ConvertToScreen(&where);
		InvokeContextMenu(where);
	}

	void InvokeContextMenu(BPoint screenWhere) {
		BPopUpMenu* menu = BuildContextMenu();
		if (!menu)
			return;

		menu->SetTargetForItems(Window());
		menu->Go(screenWhere, true, false, true);
		delete menu;
	}

	void MouseDown(BPoint where) override {
		uint32 buttons;
		GetMouse(&where, &buttons);
		bool state = Value();

		if (buttons & B_SECONDARY_MOUSE_BUTTON) {
			ConvertToScreen(&where);
			InvokeContextMenu(where);
			if (state) SetValue(B_CONTROL_ON);
		} else if (buttons & B_PRIMARY_MOUSE_BUTTON) {
			if (!Value()){
				Window()->PostMessage(kRaiseBtn);
				SetValue(B_CONTROL_ON);
				Invalidate();
			
				SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
			}
		}
	}
	
	void MouseUp(BPoint where) override {
		if (Value() == B_CONTROL_ON) {
			
			if (Bounds().Contains(where)) {
				InvokePrimaryAction();
				SetValue(B_CONTROL_ON);
			} else {
				SetValue(B_CONTROL_OFF);
			}
			Invalidate();
		}
	}
};

class MountButton : public IconButton {
public:
	MountButton(const char* name, int32 resID)
		: IconButton(name, resID, new BMessage(kShowVolumes)) {}

	void InvokePrimaryAction() override {
		Window()->PostMessage(new BMessage(kShowVolumes));
	}

	BPopUpMenu* BuildContextMenu() override {
		BPopUpMenu* menu = new BPopUpMenu("Mount Menu", false, false);
		menu->AddItem(new BMenuItem(B_TRANSLATE("Mount all"), new BMessage(kMountAllNow)));
		menu->AddItem(new BMenuItem(B_TRANSLATE("Unmount all"), new BMessage(kUnmountAll)));
		menu->AddItem(new BMenuItem(B_TRANSLATE("Settings"), //B_UTF8_ELLIPSIS, 
			new BMessage(kRunAutomounterSettings)));
		return menu;
		
	}
	void MouseDown(BPoint where) override {
		uint32 buttons;
		GetMouse(&where, &buttons);

		if (buttons & B_SECONDARY_MOUSE_BUTTON) {
			ConvertToScreen(&where);
			InvokeContextMenu(where);
		} else if (buttons & B_PRIMARY_MOUSE_BUTTON) {
			if (!Value()){
				Window()->PostMessage(kRaiseBtn);
				SetValue(B_CONTROL_ON);
				Invalidate();
			
				SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
			}
		}
		
	}
};

class ShutdownButton : public IconButton {
public:
	ShutdownButton(const char* name, int32 resID)
		: IconButton(name, resID, new BMessage(kMsgShutDown)) {}

	void InvokePrimaryAction() override {
		BRoster roster;
		BRoster::Private rosterPrivate(roster);
		rosterPrivate.ShutDown(false, true, true);
	}

	BPopUpMenu* BuildContextMenu() override {
		BPopUpMenu* menu = new BPopUpMenu("Shutdown Menu", false, false);
		menu->AddItem(new BMenuItem(B_TRANSLATE("Power Off"), new BMessage(kMsgShutDown)));
		menu->AddItem(new BMenuItem(B_TRANSLATE("Restart System"), new BMessage(kMsgReboot)));
		return menu;
	}
	
	void MouseDown(BPoint where) override {
		uint32 buttons;
		GetMouse(&where, &buttons);

		if (buttons & B_SECONDARY_MOUSE_BUTTON) {
			ConvertToScreen(&where);
			InvokeContextMenu(where);
		} else if (buttons & B_PRIMARY_MOUSE_BUTTON) {
			SetValue(B_CONTROL_ON);
			Invalidate();
			
			SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
		}
	}
	
	void MouseUp(BPoint where) override {
		if (Value() == B_CONTROL_ON) {
			SetValue(B_CONTROL_OFF);
			Invalidate();
			
			if (Bounds().Contains(where)) {
				//InvokePrimaryAction();
				
				Window()->PostMessage(kMsgAskHalt);
			}
		}
	}
};

class QueryButton : public IconButton {
public:
	QueryButton(const char* name, int32 resID)
		: IconButton(name, resID, new BMessage(kMsgQuery)) {}

	void InvokePrimaryAction() override {
		BMessenger tracker("application/x-vnd.Be-TRAK", -1);
		if (tracker.IsValid()) {
			BMessage findMessage('Tfnd');
			tracker.SendMessage(&findMessage);
		}
	}

	BPopUpMenu* BuildContextMenu() override {
		BPopUpMenu* menu = new BPopUpMenu("Query Menu", false, false);

		BMessage recentDocs;
		be_roster->GetRecentDocuments(&recentDocs, 10, "application/x-vnd.Be-query");

		type_code type;
		int32 count;
		if (recentDocs.GetInfo("refs", &type, &count) == B_OK) {
			for (int32 i = 0; i < count; i++) {
				entry_ref ref;
				if (recentDocs.FindRef("refs", i, &ref) == B_OK) {
					BEntry entry(&ref);
					if (entry.Exists()) {
						char nameBuffer[B_FILE_NAME_LENGTH];
						entry.GetName(nameBuffer);

						BPath path(&ref);
						BMessage* openQueryMsg = new BMessage(kOpenQuery);
						openQueryMsg->AddString("path", path.Path());
						menu->AddItem(new BMenuItem(nameBuffer, openQueryMsg));
					}
				}
			}
		}
		if (menu->CountItems() == 0) {
			menu->AddItem(new BMenuItem(B_TRANSLATE("<No recent queries>"), nullptr));
		}

		menu->AddSeparatorItem();
		BPath queryDirPath;
		find_directory(B_USER_DIRECTORY, &queryDirPath);
		queryDirPath.Append("queries");

		BMessage* openFolderMsg = new BMessage(kOpenQueriesFolder);
		openFolderMsg->AddString("path", queryDirPath.Path());
		menu->AddItem(new BMenuItem(B_TRANSLATE("Open Queries Folder"), openFolderMsg));

		return menu;
	}
};

class TrashButton : public IconButton {
public:
	TrashButton(const char* name, int32 resID)
		: IconButton(name, resID, new BMessage(kMsgTrash)) {}

	void InvokePrimaryAction() override {
		BPath trashPath;
		if (find_directory(B_TRASH_DIRECTORY, &trashPath) == B_OK) {
			entry_ref ref;
			if (get_ref_for_path(trashPath.Path(), &ref) == B_OK) {
				BMessenger tracker("application/x-vnd.Be-TRAK");
				if (tracker.IsValid()) {
					BMessage openMsg(B_REFS_RECEIVED);
					openMsg.AddRef("refs", &ref);
					tracker.SendMessage(&openMsg);
				}
			}
		}
	}

	BPopUpMenu* BuildContextMenu() override {
		BPopUpMenu* menu = new BPopUpMenu("Trash Menu", false, false);

		BMessage* emptyMsg = new BMessage(kMsgEmptyTrash);
		menu->AddItem(new BMenuItem(B_TRANSLATE("Empty Trash"), emptyMsg));

		return menu;
	}
};

class ButtonStripView : public BView {
private:
	std::vector<BButton*> fButtons;
	int32 fSelectedIndex;
	bool fIsVertical;
	int32 fActiveButtonIndex;
	
public:
	ButtonStripView(const char* name, bool vertical = false)
		: BView(name, B_WILL_DRAW | B_NAVIGABLE | B_FRAME_EVENTS)
		, fSelectedIndex(-1)
		, fIsVertical(vertical)
		, fActiveButtonIndex(-1)
	{
		SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	}
	
	void SetActiveButton(int32 index) {
		if (fActiveButtonIndex != index) {
			if (fActiveButtonIndex >= 0 && fActiveButtonIndex < (int32)fButtons.size()) {
				fButtons[fActiveButtonIndex]->SetValue(B_CONTROL_OFF);
			}
			
			fActiveButtonIndex = index;
			if (fActiveButtonIndex >= 0 && fActiveButtonIndex < (int32)fButtons.size()) {
				fButtons[fActiveButtonIndex]->SetValue(B_CONTROL_ON);
			}
			
			Invalidate();
		}
	}
	void InvokeButton(int32 index) {
		if (index >= 0 && index < (int32)fButtons.size()) {
			fButtons[index]->Invoke();
			fActiveButtonIndex = index;
			fButtons[index]->SetValue(B_CONTROL_ON);
		}
	}
	void RaiseButton(int32 index) {
		fButtons[index]->SetValue(B_CONTROL_OFF);
		fActiveButtonIndex=-1;
	}
	int32 GetActiveButtonIndex(void){
		return fActiveButtonIndex;
	}
	
	int32 IndexOfButton(uint32 what) {
		for (size_t i = 0; i < fButtons.size(); i++) {
			if (fButtons[i]->Message() && fButtons[i]->Message()->what == what) {
				return i;
			}
		}
		return -1;
	}
	
	void AddButton(BButton* button) {
		button->SetFlags(button->Flags() & ~B_NAVIGABLE);
		
		fButtons.push_back(button);
		AddChild(button);
		
		float buttonSize = 48;
		float spacing = 5;
		
		if (fIsVertical) {
			float y = fButtons.size() > 1 ? (fButtons.size() - 1) * (buttonSize + spacing) : 0;
			button->MoveTo(0, y);
			button->ResizeTo(buttonSize, buttonSize);
			
			float height = fButtons.size() * (buttonSize + spacing) - spacing;
			ResizeTo(buttonSize, height);
			SetExplicitMinSize(BSize(buttonSize, height));
			SetExplicitMaxSize(BSize(buttonSize, height));
		} else {
			float x = fButtons.size() > 1 ? (fButtons.size() - 1) * (buttonSize + spacing) : 0;
			button->MoveTo(x, 0);
			button->ResizeTo(buttonSize, buttonSize);
			
			float width = fButtons.size() * (buttonSize + spacing) - spacing;
			ResizeTo(width, buttonSize);
			SetExplicitMinSize(BSize(width, buttonSize));
			SetExplicitMaxSize(BSize(width, buttonSize));
		}
	}
	
	void AttachedToWindow() override {
		BView::AttachedToWindow();
		
		for (auto* button : fButtons) {
			button->SetFlags(button->Flags() & ~B_NAVIGABLE);
		}
	}
	
	void MakeFocus(bool focused) override {
		BView::MakeFocus(focused);
		
		if (focused) {
			if (fSelectedIndex < 0 && !fButtons.empty()) {
				fSelectedIndex = 0;
				_HighlightButton(fSelectedIndex);
			}
		} else {
			_HighlightButton(-1);
			fSelectedIndex = -1;
		}
		Invalidate();
	}
	
	void KeyDown(const char* bytes, int32 numBytes) override {
		if (numBytes == 1 && !fButtons.empty()) {
			int32 oldIndex = fSelectedIndex;
			
			switch (bytes[0]) {
				case B_UP_ARROW:
					if (fIsVertical && fSelectedIndex > 0) {
						fSelectedIndex--;
					}
					break;
					
				case B_DOWN_ARROW:
					if (fIsVertical && fSelectedIndex < (int32)fButtons.size() - 1) {
						fSelectedIndex++;
					}
					break;
					
				case B_LEFT_ARROW:
					if (!fIsVertical && fSelectedIndex > 0) {
						fSelectedIndex--;
					}
					break;
					
				case B_RIGHT_ARROW:
					if (!fIsVertical && fSelectedIndex < (int32)fButtons.size() - 1) {
						fSelectedIndex++;
					}
					break;
					
				case B_HOME:
					fSelectedIndex = 0;
					break;
					
				case B_END:
					fSelectedIndex = fButtons.size() - 1;
					break;

				case B_ENTER:
					if (fSelectedIndex >= 0 && fSelectedIndex < (int32)fButtons.size()) {
						if (auto* btn = dynamic_cast<IconButton*>(fButtons[fSelectedIndex])) {
							btn->InvokePrimaryAction();
						}
					}
					return;

				case B_SPACE:
					if (fSelectedIndex >= 0 && fSelectedIndex < (int32)fButtons.size()) {
						if (auto* btn = dynamic_cast<IconButton*>(fButtons[fSelectedIndex])) {
							btn->InvokeContextMenu();
						}
					}
					return;
					
				case B_TAB:
					BView::KeyDown(bytes, numBytes);
					return;
					
				default:
					BView::KeyDown(bytes, numBytes);
					return;
			}
			
			if (oldIndex != fSelectedIndex) {
				_HighlightButton(fSelectedIndex);
				Invalidate();
			}
		} else {
			BView::KeyDown(bytes, numBytes);
		}
	}
	
	void MouseDown(BPoint where) override {
		MakeFocus(true);
		
		for (size_t i = 0; i < fButtons.size(); i++) {
			if (fButtons[i]->Frame().Contains(where)) {
				fSelectedIndex = i;
				_HighlightButton(fSelectedIndex);
				Invalidate();
				break;
			}
		}
	}
	
private:
	void _HighlightButton(int32 index) {

		for (size_t i = 0; i < fButtons.size(); i++) {
			if (fButtons[i]) {
				fButtons[i]->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
				fButtons[i]->Invalidate();
			}
		}
		
		if (index >= 0 && index < (int32)fButtons.size() && fButtons[index]) {
			rgb_color highlightColor = ui_color(B_KEYBOARD_NAVIGATION_COLOR);
			fButtons[index]->SetViewColor(highlightColor);
			fButtons[index]->Invalidate();
		}
	}
};

class UnifiedListItem : public BStringItem {
public:
	UnifiedListItem(const char* name, const char* path, 
					const entry_ref& ref = entry_ref(),
					const char* sig = nullptr,
					BBitmap* icon = nullptr,
					const char* symlinkPath = nullptr,
					const char* mimeType = nullptr)
		: BStringItem("")
		, fName(name)
		, fPath(path)
		, fRef(ref)
		, fSignature(sig ? sig : "")
		, fIcon(icon)
		, fSymlinkPath(symlinkPath ? symlinkPath : "")
		, fMimeType(mimeType ? mimeType : "")
		, fUnrelated(false)
		, fIsLink(false)
	{}

	const BString& MimeType() const { return fMimeType; }

	~UnifiedListItem() { delete fIcon; }

	const BString& SymlinkPath() const { return fSymlinkPath; }
	const BString& Name() const { return fName; }
	const BString& Path() const { return fPath; }
	const entry_ref& Ref() const { return fRef; }
	const BString& Signature() const { return fSignature; }
	BBitmap* Icon() const { return fIcon; }
	
	void SetUnrelated(bool value){
		fUnrelated=value;
	}
	bool IsUnrelated(){
		return fUnrelated;
	}
	void SetLink(bool value){
		fIsLink=value;
	}
	bool IsLink(){
		return fIsLink;
	}

	void DrawItem(BView* owner, BRect frame, bool complete) override {
		rgb_color textColor = ui_color(B_LIST_ITEM_TEXT_COLOR);

		if (IsSelected()) {
			owner->SetHighColor(ui_color(B_LIST_SELECTED_BACKGROUND_COLOR));
			owner->FillRect(frame);
			textColor = ui_color(B_LIST_SELECTED_ITEM_TEXT_COLOR);
		} else if (complete) {
			owner->SetHighColor(owner->ViewColor());
			owner->FillRect(frame);
		}
		if (fUnrelated)	{
			textColor.red=255;
			textColor.green=0;
			textColor.blue=(fIsLink) ? 255 : 0;
			textColor.alpha=255;
		} else if (fIsLink) {
			textColor.blue=255;
		}

		if (fIcon && fIcon->IsValid()) {
			BRect iconRect(frame.left + 4, 
						  frame.top + 4,
						  frame.left + 36, 
						  frame.top + 36);
			owner->SetDrawingMode(B_OP_ALPHA);
			owner->DrawBitmap(fIcon, fIcon->Bounds(), iconRect);
			owner->SetDrawingMode(B_OP_COPY);
		}

		float textX = frame.left + 44;
		float maxTextWidth = frame.Width() - 48;

		BFont boldFont(be_plain_font);
		boldFont.SetFace(B_BOLD_FACE);
		owner->SetFont(&boldFont);
		owner->SetHighColor(textColor);
		
		float nameY = frame.top + 16;
		BString truncatedName(fName);
		owner->TruncateString(&truncatedName, B_TRUNCATE_END, maxTextWidth);
		owner->DrawString(truncatedName.String(), BPoint(textX, nameY));

		BFont italicFont(be_plain_font);
		italicFont.SetFace(B_ITALIC_FACE);
		owner->SetFont(&italicFont);
		
		float pathY = frame.top + 32;
		BString truncatedPath(fPath);
		owner->TruncateString(&truncatedPath, B_TRUNCATE_MIDDLE, maxTextWidth);
		owner->DrawString(truncatedPath.String(), BPoint(textX, pathY));
		
		owner->SetFont(be_plain_font);
	}

	void Update(BView* owner, const BFont* font) override {
		BStringItem::Update(owner, font);
		SetHeight(44);
	}

private:
	BString fName;
	BString fPath;
	entry_ref fRef;
	BString fSignature;
	BBitmap* fIcon;
	BString fSymlinkPath;
	BString fMimeType;
	bool fUnrelated;
	bool fIsLink;
};

class UnifiedListView : public BListView {
private:
	BPoint fDragStart;
	bool fDragging;
	int32 fDragIndex;
	BString fEmptyMessage;
	
public:
	UnifiedListView(const char* name) 
		: BListView(name)
		, fDragging(false)
		, fDragIndex(-1)
		, fEmptyMessage("<empty>") {}
	
	void SetEmptyMessage(const char* message) {
		fEmptyMessage = message;
		if (CountItems() == 0)
			Invalidate();
	}
		
	void MouseDown(BPoint where) override {
		BMessage* msg = Window()->CurrentMessage();
		int32 buttons = 0;
		msg->FindInt32("buttons", &buttons);
		
		int32 index = IndexOf(where);
		
		if (buttons & B_SECONDARY_MOUSE_BUTTON) {
			if (index >= 0) {
				Select(index);
				BMessage contextMsg(kMsgOpenMenu);
				contextMsg.AddPoint("where", where);
				contextMsg.AddInt32("index", index);
				Window()->PostMessage(&contextMsg);
			}
		} else if (buttons & B_PRIMARY_MOUSE_BUTTON) {
			if (index >= 0) {
				fDragStart = where;
				fDragging = true;
				fDragIndex = index;
				SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
			}
			BListView::MouseDown(where);
		}
	}
	
	void MouseMoved(BPoint where, uint32 transit, const BMessage* dragMessage) override {
		if (fDragging && !dragMessage && fDragIndex >= 0) {
			float distance = sqrtf(powf(where.x - fDragStart.x, 2) + powf(where.y - fDragStart.y, 2));
			if (distance > 5.0) {
				UnifiedListItem* item = dynamic_cast<UnifiedListItem*>(ItemAt(fDragIndex));
				if (item) {
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
	
	void Draw(BRect updateRect) override {
		BListView::Draw(updateRect);
		
		if (CountItems() == 0 && !fEmptyMessage.IsEmpty()) {
			SetHighColor(140, 140, 140);
			
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
	
	void MouseUp(BPoint where) override {
		fDragging = false;
		if (fDragStart == where) {
			Select(IndexOf(where));
			Window()->PostMessage(kMsgLaunch);
		}
		BListView::MouseUp(where);
	}
};

class VolumeListItem : public UnifiedListItem {
public:
	VolumeListItem(const char* name, const char* info,
				   int32 partitionId, bool isMounted,
				   int32 percent,
				   BBitmap* icon = nullptr)
		: UnifiedListItem(name, info, entry_ref(), "", icon)
		, fPartitionId(partitionId)
		, fIsMounted(isMounted)
		, fPercent(percent)
	{
	}

	int32 PartitionId() const { return fPartitionId; }
	bool IsMounted() const { return fIsMounted; }
	int32 PercentUsed() const { return fPercent; }

	void DrawItem(BView* owner, BRect frame, bool complete) override {
		UnifiedListItem::DrawItem(owner, frame, complete);

		if (!fIsMounted || fPercent < 0)
			return;

		BPoint where(frame.left + 4, frame.top + 4);
		BSize size(32, 32);

		_DrawBar(where, owner, size);
	}

private:
	int32 fPartitionId;
	bool fIsMounted;
	int32 fPercent;

	void _DrawBar(BPoint where, BView* view, BSize size) {
		view->PushState();

		int32 iconSize = size.IntegerWidth();
		int32 yOffset;
		int32 barWidth = (int32)(7.0f / 32.0f * (float)(iconSize + 1));
		if (barWidth < 4) {
			barWidth = 4;
			yOffset = 0;
		} else
			yOffset = 2;
			
		view->SetHighColor(32, 32, 32, 92);
		view->MovePenTo(BPoint(where.x + iconSize, where.y + 1 + yOffset));
		view->StrokeLine(BPoint(where.x + iconSize, where.y + iconSize - yOffset));
		view->StrokeLine(BPoint(where.x + iconSize - barWidth + 1, where.y + iconSize - yOffset));

		int32 barHeight = iconSize - 4 - 2 * yOffset;

		float left = where.x + iconSize - barWidth;
		float top = where.y + yOffset;
		float right = where.x + iconSize - 1;
		float bottom = where.y + iconSize - 1 - yOffset;
		BRect rect(left, top, right, bottom);

		view->SetDrawingMode(B_OP_ALPHA);


		view->SetHighColor(76, 76, 76, 192);
		view->StrokeRect(rect);

		int32 percent = std::max(0, std::min(100, fPercent));
		int32 barPos = int32(barHeight * percent / 100.0);

		rect.InsetBy(1,1);

		BRect freeBar(rect);
		freeBar.bottom = rect.bottom - barPos;
		if (freeBar.bottom >= freeBar.top) {
			view->SetHighColor(255, 255, 255, 192);
			view->FillRect(freeBar);
		}

		BRect usedBar(rect);
		usedBar.top = rect.bottom - barPos + 1;
		if (usedBar.top <= usedBar.bottom) {
			if (percent >= 90)
				view->SetHighColor(203, 0, 0, 192);
			else
				view->SetHighColor(0, 203, 0, 192);
			view->FillRect(usedBar);
		}

		view->PopState();
	}
};

class DeskbarWindow : public BWindow {
public:
	DeskbarWindow()
		: BWindow(BRect(-501, -601, -1, -1), "Deskbar",
			B_BORDERED_WINDOW_LOOK, B_NORMAL_WINDOW_FEEL,
			B_NOT_H_RESIZABLE | B_QUIT_ON_WINDOW_CLOSE | B_AUTO_UPDATE_SIZE_LIMITS)
		, fCurrentView(kShowApps)
		, fCloseRunner(NULL)
		, fFindThread(-1)
		, fDebounceRunner(nullptr)
		, fStopFind(false)
	{
		SetWorkspaces(B_ALL_WORKSPACES);

		fNavStrip = new ButtonStripView("nav_strip", true);
		
		IconButton* appsBtn = new IconButton("apps", kIconApps, new BMessage(kShowApps));
		IconButton* recentAppsBtn = new IconButton("recent_apps", kIconRecentApps, new BMessage(kShowRecentApps));
		IconButton* recentDocsBtn = new IconButton("recent_docs", kIconRecentDocs, new BMessage(kShowRecentDocs));
		IconButton* recentFoldersBtn = new IconButton("recent_folders", kIconRecentFolders, new BMessage(kShowRecentFolders));
		IconButton* preferencesBtn = new IconButton("preferences", kIconPreferences, new BMessage(kShowPrefs));
		IconButton* favoritesBtn = new IconButton("favorites", kIconFavorites, new BMessage(kShowFavorites));
		IconButton* goBtn = new IconButton("go", kIconGoTo, new BMessage(kShowGoTo));
		IconButton* findBtn = new IconButton("find", kIconFind, new BMessage(kShowFind));

		
		appsBtn->SetToolTip(B_TRANSLATE("Applications\tCMD+A"));
		recentAppsBtn->SetToolTip(B_TRANSLATE("Recent Applications"));
		recentDocsBtn->SetToolTip(B_TRANSLATE("Recent Documents"));
		recentFoldersBtn->SetToolTip(B_TRANSLATE("Recent Folders"));
		preferencesBtn->SetToolTip(B_TRANSLATE("Preferences"));
		favoritesBtn->SetToolTip(B_TRANSLATE("Favorites\tCMD+S"));
		goBtn->SetToolTip(B_TRANSLATE("Go\tCMD+G"));
		findBtn->SetToolTip(B_TRANSLATE("Find\tCMD+F"));
		
		fNavStrip->AddButton(appsBtn);
		fNavStrip->AddButton(recentAppsBtn);
		fNavStrip->AddButton(recentDocsBtn);
		fNavStrip->AddButton(recentFoldersBtn);
		fNavStrip->AddButton(preferencesBtn);
		fNavStrip->AddButton(favoritesBtn);
		fNavStrip->AddButton(goBtn);
		fNavStrip->AddButton(findBtn);
		
		fActionStrip = new ButtonStripView("action_strip", false);
		
		ShutdownButton* shutdownBtn = new ShutdownButton("shutdown", kIconShutdown);
		MountButton* mountBtn = new MountButton("mount", kIconMount);
		QueryButton* queryBtn = new QueryButton("query", kIconQuery);
		TrashButton* trashBtn = new TrashButton("trash", kIconTrash);
		IconButton* prefsBtn = new IconButton("prefs", kIconPrefs, new BMessage(kMsgPrefs));
		IconButton* aboutBtn = new IconButton("about", kIconAbout, new BMessage(kMsgAbout));
		
		shutdownBtn->SetToolTip(B_TRANSLATE("Shutdown"));
		mountBtn->SetToolTip(B_TRANSLATE("Disks\tCMD+D"));
		queryBtn->SetToolTip(B_TRANSLATE("Query"));
		trashBtn->SetToolTip(B_TRANSLATE("Trash"));
		prefsBtn->SetToolTip(B_TRANSLATE("Deskbar Preferences"));
		aboutBtn->SetToolTip(B_TRANSLATE("About"));
		
		fActionStrip->AddButton(shutdownBtn);
		fActionStrip->AddButton(mountBtn);
		fActionStrip->AddButton(queryBtn);
		fActionStrip->AddButton(trashBtn);
		fActionStrip->AddButton(prefsBtn);
		fActionStrip->AddButton(aboutBtn);
		
		fMainList = new UnifiedListView("main_list");
		fMainList->SetInvocationMessage(new BMessage(kMsgLaunch));
		fMainList->SetTarget(this);

		BScrollView* listScroll = new BScrollView("list_scroll",
			fMainList, 0, false, true);

		fFilter = new BTextControl("", "", "", new BMessage(kMsgApplyFilter));
		fFilter->SetTarget(this);
		fFilter->SetModificationMessage(new BMessage(kMsgApplyFilter));

		fContentView = new BGroupView(B_VERTICAL, 5);
		
		BLayoutBuilder::Group<>(fContentView, B_VERTICAL, 0)
			.Add(listScroll, 1.0f)
			.Add(fFilter);
				
		BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
			.SetInsets(10)
			.Add(fActionStrip)
			.AddGroup(B_HORIZONTAL, 5)
				.AddGroup(B_VERTICAL, 5)
					.Add(fContentView, 1.0f)
				.End()
				.AddGroup(B_VERTICAL, 5)
					.Add(fNavStrip)
				.End()
			.End();
		
		_LoadApplications();
				
		fMainList->SetFlags(fMainList->Flags() & ~B_NAVIGABLE);
		
		fFilter->MakeFocus(true);
		
		BString filterTooltip = B_TRANSLATE("Type to filter.");
		filterTooltip << B_TRANSLATE("\nUse:\narrow keys to move through the list;\nENTER to launch;\nF10 to open the menu.");
		
		fFilter->SetToolTip(filterTooltip);

		//CenterOnScreen();
		

		fNavStrip->InvokeButton(5); //Favorites
		BMessage reply;
		BMessenger msngr("application/x-vnd.Be-TSKB"); //the deskbar
		msngr.SendMessage(kPressDeskbarBtn,&reply);
	}
	
	~DeskbarWindow() {
		fStopFind = true;
		if (fFindThread >= 0) {
			wait_for_thread(fFindThread, NULL);
		}
		delete fDebounceRunner;
		delete fCloseRunner;
	}
	
	void WindowActivated(bool active) override {
		BWindow::WindowActivated(active);
		
		if (!active) {
			delete fCloseRunner;
			BMessage reply;
			BMessenger msngr("application/x-vnd.Be-TSKB");
			msngr.SendMessage(kRaiseDeskbarBtn,&reply);
			BMessage closeMsg(kMsgDelayedClose);
			fCloseRunner = new BMessageRunner(this, &closeMsg, 50000, 1);
		} else {
			BPoint targetPoint = CalculateAppPosition(Bounds());
			MoveTo(targetPoint);
			delete fCloseRunner;
			fCloseRunner = NULL;
		}
	}

	void MessageReceived(BMessage* msg) override {
		switch (msg->what) {
		
			case kMsgDelayedClose:
				if (!IsActive()) {
					PostMessage(B_QUIT_REQUESTED);
				}
				break;
				
			case kRaiseBtn:
				if (fNavStrip->GetActiveButtonIndex()>-1) fNavStrip->RaiseButton(fNavStrip->GetActiveButtonIndex());
				if (fActionStrip->GetActiveButtonIndex()>-1) fActionStrip->RaiseButton(fActionStrip->GetActiveButtonIndex());
				break;
				
			case kShowApps:
				fCurrentView = kShowApps;
				fFilter->SetText("");
				fFilter->MakeFocus(true);
				_ApplyFilter("");
				fNavStrip->SetActiveButton(fNavStrip->IndexOfButton(kShowApps));
				break;
			
			case kShowPrefs:
				fCurrentView = kShowPrefs;
				fFilter->SetText("");
				fFilter->MakeFocus(true);
				_ApplyFilter("");
				fNavStrip->SetActiveButton(fNavStrip->IndexOfButton(kShowPrefs));
				break;
			

			case kShowRecentApps:
				fCurrentView = kShowRecentApps;
				fFilter->SetText("");
				fFilter->MakeFocus(true);
				_ApplyFilter("");
				fNavStrip->SetActiveButton(fNavStrip->IndexOfButton(kShowRecentApps));
				break;

			case kShowRecentDocs:
				fCurrentView = kShowRecentDocs;
				fFilter->SetText("");
				fFilter->MakeFocus(true);
				_ApplyFilter("");
				fNavStrip->SetActiveButton(fNavStrip->IndexOfButton(kShowRecentDocs));
				break;

			case kShowRecentFolders:
				fCurrentView = kShowRecentFolders;
				fFilter->SetText("");
				fFilter->MakeFocus(true);
				_ApplyFilter("");
				fNavStrip->SetActiveButton(fNavStrip->IndexOfButton(kShowRecentFolders));
				break;

			case kShowFavorites:
				fCurrentView = kShowFavorites;
				fFilter->SetText("");
				fFilter->MakeFocus(true);
				_ApplyFilter("");
				fNavStrip->SetActiveButton(fNavStrip->IndexOfButton(kShowFavorites));
				break;

			case kShowGoTo:
				fCurrentView = kShowGoTo;
				fFilter->SetText("");
				fFilter->MakeFocus(true);
				_ApplyFilter("");
				fNavStrip->SetActiveButton(fNavStrip->IndexOfButton(kShowGoTo));
				break;
				
			case kShowFind:
				fCurrentView = kShowFind;
				fFilter->SetText("");
				fFilter->MakeFocus(true);
				_ApplyFilter("");
				fNavStrip->SetActiveButton(fNavStrip->IndexOfButton(kShowFind));
				break;
				
			case kShowVolumes:
				fCurrentView = kShowVolumes;
				fFilter->SetText("");
				fFilter->MakeFocus(true);
				_ApplyFilter("");
				fActionStrip->SetActiveButton(fActionStrip->IndexOfButton(kShowVolumes));
				break;

			case kToggleVolume:
			{
				int32 partitionId;
				bool mount;
				if (msg->FindInt32("partition_id", &partitionId) == B_OK &&
					msg->FindBool("mount", &mount) == B_OK) {
					
					struct MountData {
						int32 partitionId;
						bool mount;
					};
					
					MountData* data = new MountData;
					data->partitionId = partitionId;
					data->mount = mount;
					
					thread_id thread = spawn_thread(_MountWorker, "mount_worker",
												  B_NORMAL_PRIORITY, data);
					if (thread >= 0) {
						resume_thread(thread);
						
						BMessage refreshMsg(kShowVolumes);
						BMessageRunner::StartSending(this, &refreshMsg, 500000, 1);
					}
				}
				break;
			}

			case kUnmountAll:
			{
				thread_id thread = spawn_thread(_UnmountAllWorker, "unmount_all",
												B_NORMAL_PRIORITY, this);
				if (thread >= 0)
					resume_thread(thread);
				break;
			}
			
			case kMsgGetInfo: {
				entry_ref ref;
				if (msg->FindRef("refs", &ref) == B_OK) {
					BPath path(&ref);
					OpenInfo(path.Path());
				}
				break;
			}
				
			case kMsgApplyFilter: {
				delete fDebounceRunner;
				fDebounceRunner = nullptr;

				BMessage delayed(kMsgDeleteFilter);
				fDebounceRunner = new BMessageRunner(this, &delayed,
													 kDebounceTime, 1);
				break;
			}

			case kMsgDeleteFilter: {
				delete fDebounceRunner;
				fDebounceRunner = nullptr;

				BString filterText = fFilter->Text();
				_ApplyFilter(filterText);
				break;
			}
				
			case kMsgLaunch:
				_LaunchSelectedItem();
				break;
				
			case kMsgOpenMenu:
			{
				BPoint where;
				int32 index;
				if (msg->FindPoint("where", &where) == B_OK &&
					msg->FindInt32("index", &index) == B_OK) {
					_ShowContextMenu(where, index);
				}
				break;
			}
				
			case kMsgRevealInTracker:
			{
				entry_ref ref;
				if (msg->FindRef("refs", &ref) == B_OK) {
					RevealInTracker(ref);
				}
				break;
			}
			
			case kMsgAbout:
				be_roster->Launch("application/x-vnd.Haiku-About");
				fActionStrip->SetActiveButton(fActionStrip->IndexOfButton(kMsgAbout));

				break;
				
			case kMsgPrefs:
				be_roster->Launch("application/x-vnd.Haiku-DeskbarPreferences");
				fActionStrip->SetActiveButton(fActionStrip->IndexOfButton(kMsgPrefs));
				break;
				
			case kMsgShutDown: {
				BRoster r;
				BRoster::Private rp(r);
				rp.ShutDown(false, false, true);
				break;
			}
			
			case kMsgReboot: {
				BRoster r;
				BRoster::Private rp(r);
				rp.ShutDown(true, false, true);
				break;
			}
			
			case kMsgAskHalt: {
				BRoster roster;
				BRoster::Private rosterPrivate(roster);
				rosterPrivate.ShutDown(false, true, true);
			}
			
			case kMountVolume:
			{
				int32 id;
				if (msg->FindInt32("id", &id) == B_OK) {
					BDiskDeviceList devices;
					BDiskDevice device;
					BPartition* partition;
					
					if (devices.Fetch() == B_OK) {
						partition = devices.PartitionWithID(id);
						if (partition) {
							partition->Mount();
						}
					}
				}
				break;
			}

			case kUnmountVolume:
			{
				int32 id;
				if (msg->FindInt32("id", &id) == B_OK) {
					BDiskDeviceList devices;
					BDiskDevice device;
					BPartition* partition;
					
					if (devices.Fetch() == B_OK) {
						partition = devices.PartitionWithID(id);
						if (partition && partition->IsMounted()) {
							partition->Unmount();
						}
					}
				}
				
				int32 deviceId;
				if (msg->FindInt32("device_id", &deviceId) == B_OK) {
					BVolume volume(deviceId);
					if (volume.InitCheck() == B_OK) {
						BMessenger tracker("application/x-vnd.Be-TRAK");
						BMessage unmountMsg('Tunm');
						
						BDirectory rootDir;
						volume.GetRootDirectory(&rootDir);
						BEntry entry;
						rootDir.GetEntry(&entry);
						entry_ref ref;
						entry.GetRef(&ref);
						
						unmountMsg.AddRef("refs", &ref);
						tracker.SendMessage(&unmountMsg);
					}
				}
				break;
			}

			case kMountAllNow:
			{
				thread_id thread = spawn_thread(_MountAllWorker, "mount_all",
												B_NORMAL_PRIORITY, this);
				if (thread >= 0)
					resume_thread(thread);
				break;
			}

			case kRunAutomounterSettings:
			{
				BMessenger tracker("application/x-vnd.Be-TRAK");
				if (tracker.IsValid()) {
					BMessage cmd('Tram');
					tracker.SendMessage(&cmd);
				}
				break;
			}
			
			case kMsgAddFavorite:
			{
				entry_ref ref;
				if (msg->FindRef("refs", &ref) == B_OK) {
					_AddToFavorites(ref);
				}
				break;
			}

			case kMsgAddGoTo:
			{
				entry_ref ref;
				if (msg->FindRef("refs", &ref) == B_OK) {
					_AddToGoTo(ref);
				}
				break;
			}

			case kMsgRemoveItem:
			{
				BString symlinkPath;
				int32 itemIndex;
				if (msg->FindString("symlink_path", &symlinkPath) == B_OK &&
					msg->FindInt32("index", &itemIndex) == B_OK) {
					
					BEntry symlink(symlinkPath.String());
					if (symlink.InitCheck() == B_OK && symlink.Exists()) {
						symlink.Remove();
					}
					
					if (itemIndex >= 0 && itemIndex < fMainList->CountItems()) {
						BListItem* item = fMainList->RemoveItem(itemIndex);
						delete item;
						
						if (fMainList->CountItems() == 0) {
							fMainList->Invalidate();
						}
						
						int32 newSelection = itemIndex;
						if (newSelection >= fMainList->CountItems() && newSelection > 0) {
							newSelection--;
						}
						if (newSelection < fMainList->CountItems()) {
							fMainList->Select(newSelection);
							fMainList->ScrollToSelection();
						}
					}
				}
				break;
			}
			
			case kMsgEmptyTrash: {
				BMessenger tracker("application/x-vnd.Be-TRAK");
				if (tracker.IsValid()) {
					BMessage cmd(B_DELETE_PROPERTY);
					cmd.AddSpecifier("Trash");
					tracker.SendMessage(&cmd);
				}
				break;
			}
				
			case kOpenQuery:
			{
				BString path;
				if (msg->FindString("path", &path) == B_OK) {
					entry_ref ref;
					if (get_ref_for_path(path.String(), &ref) == B_OK) {
						be_roster->Launch(&ref);
					}
				}
				break;
			}
			
			case kOpenQueriesFolder:
			{
				BString path;
				if (msg->FindString("path", &path) == B_OK) {
					entry_ref ref;
					if (get_ref_for_path(path.String(), &ref) == B_OK) {
						BMessenger tracker("application/x-vnd.Be-TRAK");
						BMessage openMsg(B_REFS_RECEIVED);
						openMsg.AddRef("refs", &ref);
						tracker.SendMessage(&openMsg);
					}
				}
				break;
			}
			
			case kFindResults: {
				entry_ref ref;
				for (int32 i = 0; msg->FindRef("refs", i, &ref) == B_OK; i++) {
					BEntry entry(&ref, true);
					if (!entry.Exists())
						continue;

					BPath path;
					entry.GetPath(&path);
					BPath parentPath;
					path.GetParent(&parentPath);

					BBitmap* icon = LoadBestIconForRef(ref, nullptr, 32);

					auto* item = new UnifiedListItem(
						ref.name,
						parentPath.Path(),
						ref,
						"",
						icon
					);

					_InsertSortedItem(item);
				}

				if (fMainList->CurrentSelection() < 0 && fMainList->CountItems() > 0) {
					fMainList->Select(0);
					fMainList->ScrollToSelection();
				}
				break;
			}

			case kFindDone:
				break;
				
			default:
				BWindow::MessageReceived(msg);
				break;
		}
	}

private:
	UnifiedListView* fMainList;
	BTextControl* fFilter;
	BView* fContentView;
	ButtonStripView* fNavStrip;
	ButtonStripView* fActionStrip;
	uint32 fCurrentView;
	BMessageRunner* fCloseRunner;
	thread_id fFindThread;
	BMessageRunner* fDebounceRunner;
	static constexpr bigtime_t kDebounceTime = 250000;
	BString fLastFilterApplied;
	uint32  fLastViewApplied = 0;
	
	std::atomic<bool> fStopFind;
	std::vector<std::pair<BString, UnifiedListItem*>> fAllAppsMaster;
	
	void _AddToFavorites(const entry_ref& ref) {
		BPath userSettings;
		if (find_directory(B_USER_DESKBAR_DIRECTORY, &userSettings) != B_OK)
			return;
		userSettings.Append("Favorites");
		{
			BEntry a(userSettings.Path());
			if (!a.Exists()) {
				create_directory(userSettings.Path(), 0640);
			}
		}
		_CreateSymlink(ref, userSettings.Path());
		
		if (fCurrentView == kShowFavorites) {
			_ApplyFilter(fFilter->Text());
		}
	}

	void _AddToGoTo(const entry_ref& ref) {
		BPath userSettings;
		if (find_directory(B_USER_SETTINGS_DIRECTORY, &userSettings) != B_OK)
			return;
		userSettings.Append("Tracker/Go");
		_CreateSymlink(ref, userSettings.Path());
		
		if (fCurrentView == kShowGoTo) {
			_ApplyFilter(fFilter->Text());
		}
	}

	void _CreateSymlink(const entry_ref& ref, const char* targetDir) {
		BDirectory dir(targetDir);
		if (dir.InitCheck() != B_OK) {
			create_directory(targetDir, 0755);
			dir.SetTo(targetDir);
		}
		
		BEntry entry(&ref);
		BPath sourcePath;
		entry.GetPath(&sourcePath);
		
		BString linkName(ref.name);
		BPath linkPath(targetDir);
		linkPath.Append(linkName.String());
		
		int counter = 1;
		while (BEntry(linkPath.Path()).Exists()) {
			linkName = ref.name;
			linkName << " " << counter++;
			linkPath.SetTo(targetDir);
			linkPath.Append(linkName.String());
		}
		
		dir.CreateSymLink(linkName.String(), sourcePath.Path(), NULL);
	}


	
	void _LoadVolumes(BList* outList) {
		if (!outList)
			return;

		BDiskDeviceList devices;
		if (devices.Fetch() != B_OK)
			return;

		class VolumeVisitor : public BDiskDeviceVisitor {
		public:
			VolumeVisitor(BList* list) : fList(list) {}

			virtual bool Visit(BDiskDevice* device) {
				return Visit(device, 0);
			}

			virtual bool Visit(BPartition* partition, int32 level) {
				if (!partition->ContainsFileSystem())
					return false;

				BString name = partition->ContentName();
				if (name.Length() == 0) {
					name = partition->Name();
					if (name.Length() == 0) {
						const char* type = partition->ContentType();
						if (type == NULL)
							return false;

						uint32 divisor = 1UL << 30;
						char unit = 'G';
						if (partition->Size() < divisor) {
							divisor = 1UL << 20;
							unit = 'M';
						}

						name.SetToFormat("%.1f %cB %s",
							1.0 * partition->Size() / divisor, unit, type);
					}
				}

				BString info;
				int32 percentUsed = -1;

				if (partition->IsMounted()) {
					BVolume volume;
					if (partition->GetVolume(&volume) == B_OK) {
						BDirectory rootDir;
						volume.GetRootDirectory(&rootDir);
						BPath path(&rootDir, ".");
						info = path.Path();

						off_t capacity = volume.Capacity();
						off_t freeBytes = volume.FreeBytes();
						if (capacity > 0) {
							percentUsed = (int32)((100.0 * (capacity - freeBytes)) 
												   / (double)capacity + 0.5);
						}
					}
				} else {
					info = B_TRANSLATE("Not mounted");
				}

				BBitmap* icon = nullptr;
				if (partition->IsMounted()) {
					BVolume volume;
					if (partition->GetVolume(&volume) == B_OK) {
						icon = new BBitmap(BRect(0, 0, 31, 31), B_RGBA32);
						if (volume.GetIcon(icon, B_LARGE_ICON) != B_OK) {
							delete icon;
							icon = LoadVectorIcon(kIconMount, 32);
						}
					}
				} else {
					icon = LoadVectorIcon(kIconMount, 32);
				}

				VolumeListItem* item = new VolumeListItem(
					name.String(),
					info.String(),
					partition->ID(),
					partition->IsMounted(),
					percentUsed,
					icon
				);

				fList->AddItem(item);
				return false;
			}

		private:
			BList* fList;
		} visitor(outList);

		devices.VisitEachPartition(&visitor);

		BVolumeRoster volumeRoster;
		BVolume volume;
		while (volumeRoster.GetNextVolume(&volume) == B_OK) {
			if (volume.IsShared()) {
				char volumeName[B_FILE_NAME_LENGTH];
				volume.GetName(volumeName);

				BDirectory rootDir;
				volume.GetRootDirectory(&rootDir);
				BPath path(&rootDir, ".");

				int32 percentUsed = -1;
				off_t capacity = volume.Capacity();
				off_t freeBytes = volume.FreeBytes();
				if (capacity > 0) {
					percentUsed = (int32)((100.0 * (capacity - freeBytes)) 
										   / (double)capacity + 0.5);
				}

				VolumeListItem* item = new VolumeListItem(
					volumeName,
					path.Path(),
					-volume.Device(),
					true,
					percentUsed,
					LoadVectorIcon(kIconMount, 32)
				);

				outList->AddItem(item);
			}
		}
	}
		
	void _LoadApplications() {
		fMainList->MakeEmpty();
		fAllAppsMaster.clear();
		BPath sysPath;
		
		if (find_directory(B_SYSTEM_APPS_DIRECTORY, &sysPath) == B_OK) {
			_ScanAppsDirectory(sysPath.Path());
		}
				
		BPath userAppsPath;
		if (find_directory(B_USER_NONPACKAGED_BIN_DIRECTORY, &userAppsPath) == B_OK) {
			_ScanAppsDirectory(userAppsPath.Path());
		}
		
		if (find_directory(B_USER_APPS_DIRECTORY, &userAppsPath) == B_OK) {
			_ScanAppsDirectory(userAppsPath.Path());
		}
				
		std::sort(fAllAppsMaster.begin(), fAllAppsMaster.end(),
			[](const auto& a, const auto& b) {
				return strcasecmp(a.first.String(), b.first.String()) < 0;
			});
		
		for (auto& pair : fAllAppsMaster) {
			fMainList->AddItem(pair.second);
		}
	}
	
	void _ScanAppsDirectory(const char* dirPath, int depth = 0) {
		BDirectory dir(dirPath);
		if (dir.InitCheck() != B_OK) return;

		BEntry entry;
		while (dir.GetNextEntry(&entry, false) == B_OK) {
			if (!entry.Exists())
				continue;

			if (entry.IsDirectory() && depth < 1) {
				BPath subPath;
				if (entry.GetPath(&subPath) == B_OK) {
					_ScanAppsDirectory(subPath.Path(), depth + 1);
				}
				continue;
			}

			BFile file(&entry, B_READ_ONLY);
			if (file.InitCheck() != B_OK) continue;

			BAppFileInfo appInfo(&file);
			char signature[B_MIME_TYPE_LENGTH];

			if (appInfo.GetSignature(signature) == B_OK && signature[0] != '\0') {
				entry_ref ref;
				if (entry.GetRef(&ref) != B_OK) continue;

				BString appName = _GetAppName(signature, ref);
				BPath path;
				entry.GetPath(&path);
				
				BPath parentPath;
				path.GetParent(&parentPath);

				BBitmap* icon = LoadBestIconForRef(ref, signature, 32);

				UnifiedListItem* item = new UnifiedListItem(
					appName.String(), 
					parentPath.Path(),
					ref, 
					signature, 
					icon
				);
				
				fAllAppsMaster.emplace_back(appName, item);
			}
		}
	}
	
	void _LoadPreferences(BList* outList) {
		if (!outList)
			return;
		
		BPath prefsPath;
		if (find_directory(B_SYSTEM_PREFERENCES_DIRECTORY, &prefsPath) != B_OK)
			return;
		
		BDirectory dir(prefsPath.Path());
		if (dir.InitCheck() != B_OK)
			return;
		
		_ScanPrefsDirectory(outList, prefsPath.Path());
		
		BPath userPrefsPath;
		if (find_directory(B_USER_PREFERENCES_DIRECTORY, &userPrefsPath) != B_OK)
			return;
		
		BDirectory userDir(userPrefsPath.Path());
		if (userDir.InitCheck() != B_OK)
			return;
		
		_ScanPrefsDirectory(outList, userPrefsPath.Path());

	}
	
	void _ApplyFilter(const BString& filter) {
		if (fCurrentView == kShowVolumes) {
		} else if (filter == fLastFilterApplied && fCurrentView == fLastViewApplied) {
			return;
		}

		fLastFilterApplied = filter;
		fLastViewApplied = fCurrentView;

		fMainList->MakeEmpty();	
		
		BString filterTooltip = B_TRANSLATE("Type to filter.");

		if (fCurrentView == kShowFind) {
			
			filterTooltip = B_TRANSLATE("Type at least 3 characters to start a search.");
			if (filter.Length() < 3) {
				fMainList->SetEmptyMessage(B_TRANSLATE("Type your search"));
			} else {
				fMainList->SetEmptyMessage(B_TRANSLATE("Searching"));
			}
		} else {
			if (!filter.IsEmpty()) {
				fMainList->SetEmptyMessage(B_TRANSLATE("No matches"));
			} else {
				fMainList->SetEmptyMessage(B_TRANSLATE("<empty>"));
			}
		}
		
		
		filterTooltip << B_TRANSLATE("\nUse:\narrow keys to move through the list;\nENTER to launch;\nF10 to open the menu.");
		
		fFilter->SetToolTip(filterTooltip);

		switch (fCurrentView) {
			case kShowApps:
			{
				for (auto& pair : fAllAppsMaster) {
					UnifiedListItem* item = pair.second;
					if (filter.IsEmpty() ||
						item->Name().IFindFirst(filter) >= 0 ||
						item->Path().IFindFirst(filter) >= 0) {
						item->SetUnrelated(false);
						fMainList->AddItem(item);
					}
				}
				break;
			}
			
			case kShowPrefs:
			{
				BList temp;
				_LoadPreferences(&temp);
				for (int32 i = 0; i < temp.CountItems(); i++) {
					UnifiedListItem* item = (UnifiedListItem*)temp.ItemAt(i);
					if (filter.IsEmpty() ||
						item->Name().IFindFirst(filter) >= 0 ||
						item->Path().IFindFirst(filter) >= 0) {
						item->SetUnrelated(false);
						fMainList->AddItem(item);
					} else {
						delete item;
					}
				}
				
				break;
			}
			
			case kShowVolumes:
			{
				BList temp;
				_LoadVolumes(&temp);
				
				for (int32 i = 0; i < temp.CountItems(); i++) {
					VolumeListItem* item = (VolumeListItem*)temp.ItemAt(i);
					if (filter.IsEmpty() ||
						item->Name().IFindFirst(filter) >= 0 ||
						item->Path().IFindFirst(filter) >= 0) {
						fMainList->AddItem(item);
					} else {
						delete item;
					}
				}
				break;
			}
			case kShowFavorites:
			{
				BList temp;
				_LoadFavorites(&temp);
				
				for (int32 i = 0; i < temp.CountItems(); i++) {
					UnifiedListItem* item = (UnifiedListItem*)temp.ItemAt(i);
					if (filter.IsEmpty() ||
						item->Name().IFindFirst(filter) >= 0 ||
						item->Path().IFindFirst(filter) >= 0) {
						item->SetUnrelated(false);
						fMainList->AddItem(item);
					} else {
						delete item;
					}
				}
				
				if (fMainList->CountItems() > 0) {
					fMainList->Select(0);
					fMainList->ScrollToSelection();
				} else if (filter.Length() >= 3) {
					for (auto& pair : fAllAppsMaster) {
						UnifiedListItem* item = pair.second;
						if (filter.IsEmpty() ||
							item->Name().IFindFirst(filter) >= 0 ||
							item->Path().IFindFirst(filter) >= 0) {
							item->SetUnrelated(true);
							fMainList->AddItem(item);
						}
					}
				} else {
					fNavStrip->InvokeButton(0);
				}
				
				break;
			}
			case kShowRecentApps:
			case kShowRecentDocs:
			case kShowRecentFolders:
			case kShowGoTo:
			{
				BList temp;

				switch (fCurrentView) {
					case kShowRecentApps:
						_LoadRecentApps(&temp);
						break;
					case kShowRecentDocs:
						_LoadRecentDocs(&temp);
						break;
					case kShowRecentFolders:
						_LoadRecentFolders(&temp);
						break;
					case kShowFavorites:
						_LoadFavorites(&temp);
						break;
					case kShowGoTo:
						_LoadGoTo(&temp);
						break;
					default:
						break;
				}

				for (int32 i = 0; i < temp.CountItems(); i++) {
					UnifiedListItem* item = (UnifiedListItem*)temp.ItemAt(i);
					if (filter.IsEmpty() ||
						item->Name().IFindFirst(filter) >= 0 ||
						item->Path().IFindFirst(filter) >= 0) {
						item->SetUnrelated(false);
						fMainList->AddItem(item);
					} else {
						delete item;
					}
				}
				
				if (fCurrentView == kShowFind && filter.Length() >= 3 && fMainList->CountItems() == 0) {
						fMainList->SetEmptyMessage(B_TRANSLATE("No matches"));
					}

				if (fMainList->CountItems() > 0) {
					fMainList->Select(0);
					fMainList->ScrollToSelection();
				}
				
				break;
			}

			case kShowFind:
			{
				fMainList->MakeEmpty();

				if (filter.Length() < 3)
					break;

				BString pattern = BuildCaseInsensitivePattern(filter, true);

				std::vector<UnifiedListItem*> foundItems;

				BVolumeRoster roster;
				BVolume volume;
				while (roster.GetNextVolume(&volume) == B_OK) {
					if (!volume.IsPersistent() || !volume.KnowsQuery())
						continue;

					BQuery query;
					query.SetVolume(&volume);

					BString predicate;
					predicate.SetToFormat("name=%s", pattern.String());
					query.SetPredicate(predicate.String());

					if (query.Fetch() == B_OK) {
						entry_ref ref;
						while (query.GetNextRef(&ref) == B_OK) {
							//check if it is a link to set the blue color
							BEntry linkCheck(&ref, false);
							bool isLink = linkCheck.IsSymLink();
							
							BEntry entry(&ref, true);
							if (!entry.Exists())
								continue;

							BPath path;
							entry.GetPath(&path);
							BPath parentPath;
							path.GetParent(&parentPath);

							BBitmap* icon = nullptr;
							BString mime;
							BString signature;

							BNode node(&ref);
							if (node.InitCheck() == B_OK) {
								BNodeInfo nodeInfo(&node);
								char buf[B_MIME_TYPE_LENGTH] = {};
								if (nodeInfo.GetType(buf) == B_OK && buf[0] != '\0') {
									mime = buf;
									
									if (strstr(buf, "application/x-vnd.") == buf) {
										BFile file(&ref, B_READ_ONLY);
										if (file.InitCheck() == B_OK) {
											BAppFileInfo appInfo(&file);
											char sig[B_MIME_TYPE_LENGTH];
											if (appInfo.GetSignature(sig) == B_OK) {
												signature = sig;
											}
										}
									}
								}
							}

							icon = LoadBestIconForRef(ref, signature.String(), 32);

							UnifiedListItem* item = new UnifiedListItem(
								ref.name,
								parentPath.Path(),
								ref,
								signature.String(),
								icon,
								nullptr,
								mime.String()
							);
							
							if (isLink) {
								item->SetLink(true);
							}

							foundItems.push_back(item);
						}
					}
				}

				std::sort(foundItems.begin(), foundItems.end(),
					[](UnifiedListItem* a, UnifiedListItem* b) {
						int cmp = strcasecmp(a->Signature().String(), b->Signature().String());
						if (cmp == 0)
							cmp = strcasecmp(a->Name().String(), b->Name().String());
						return cmp < 0;
					});

				for (auto* item : foundItems)
					fMainList->AddItem(item);

				break;
			}
		}

		if (fMainList->CountItems() > 0) {
			fMainList->Select(0);
			fMainList->ScrollToSelection();
		}
	}
	  
	void _LoadRecentApps(BList* outList) {
		if (!outList)
			return;

		BMessage list;
		be_roster->GetRecentApps(&list, 10);

		int32 count = 0;
		list.GetInfo("refs", NULL, &count);

		for (int32 i = 0; i < count; i++) {
			entry_ref ref;
			if (list.FindRef("refs", i, &ref) == B_OK) {
				BEntry entry(&ref, true);
				if (entry.Exists()) {
					BString signature;
					BFile f(&ref, B_READ_ONLY);
					if (f.InitCheck() == B_OK) {
						BAppFileInfo afi(&f);
						char sig[B_MIME_TYPE_LENGTH];
						if (afi.GetSignature(sig) == B_OK)
							signature = sig;
					}

					BString label = _GetAppName(signature.String(), ref);

					BPath path;
					entry.GetPath(&path);
					BPath parentPath;
					path.GetParent(&parentPath);

					BBitmap* icon = LoadBestIconForRef(ref, signature.String(), 32);

					UnifiedListItem* item = new UnifiedListItem(
						label.String(),
						parentPath.Path(),
						ref,
						signature.String(),
						icon
					);

					outList->AddItem(item);
				}
			}
		}
	}
	
	void _LoadRecentDocs(BList* outList) {
		if (!outList)
			return;

		BMessage list;
		be_roster->GetRecentDocuments(&list, 10);

		int32 count = 0;
		list.GetInfo("refs", NULL, &count);

		for (int32 i = 0; i < count; i++) {
			entry_ref ref;
			if (list.FindRef("refs", i, &ref) == B_OK) {
				BEntry entry(&ref, true);
				if (entry.Exists()) {
					BPath path;
					entry.GetPath(&path);
					BPath parentPath;
					path.GetParent(&parentPath);

					BBitmap* icon = LoadBestIconForRef(ref, NULL, 32);

					UnifiedListItem* item = new UnifiedListItem(
						ref.name,
						parentPath.Path(),
						ref,
						"",
						icon
					);
					outList->AddItem(item);
				}
			}
		}
	}
	
	void _LoadRecentFolders(BList* outList) {
		if (!outList)
			return;

		BMessage list;
		be_roster->GetRecentFolders(&list, 10);

		int32 count = 0;
		list.GetInfo("refs", NULL, &count);

		for (int32 i = 0; i < count; i++) {
			entry_ref ref;
			if (list.FindRef("refs", i, &ref) == B_OK) {
				BEntry entry(&ref, true);
				if (entry.Exists() && entry.IsDirectory()) {
					BPath path;
					entry.GetPath(&path);
					BPath parentPath;
					path.GetParent(&parentPath);

					BBitmap* icon = LoadBestIconForRef(
						ref, 
						"application/x-vnd.Be-directory", 
						32
					);

					UnifiedListItem* item = new UnifiedListItem(
						ref.name,
						parentPath.Path(),
						ref,
						"application/x-vnd.Be-directory",
						icon
					);
					outList->AddItem(item);
				}
			}
		}
	}
	
	void _ScanPrefsDirectory(BList* outList, const char* dirPath, int depth = 0) {
		BDirectory dir(dirPath);
		if (dir.InitCheck() != B_OK)
			return;

		BEntry entry;
		while (dir.GetNextEntry(&entry, false) == B_OK) {
			entry_ref ref;
			if (entry.GetRef(&ref) != B_OK)
				continue;
				
			BPath symlinkPath;
			entry.GetPath(&symlinkPath);

			BEntry target(&ref, true);
			if (target.InitCheck() != B_OK || !target.Exists())
				continue;

			entry_ref targetRef;
			if (target.GetRef(&targetRef) != B_OK)
				continue;

			BPath path;
			target.GetPath(&path);
			BPath parentPath;
			path.GetParent(&parentPath);

			BNode node(&target);
			BNodeInfo nodeInfo(&node);
			char mimeType[B_MIME_TYPE_LENGTH] = {};

			BString signature;
			BString name = targetRef.name;
			BBitmap* icon = nullptr;

			if (nodeInfo.GetType(mimeType) == B_OK) {
				if (strstr(mimeType, "application/x-vnd.") == mimeType) {
					BFile f(&targetRef, B_READ_ONLY);
					BAppFileInfo appInfo(&f);
					char sig[B_MIME_TYPE_LENGTH];
					if (appInfo.GetSignature(sig) == B_OK) {
						signature = sig;
						name = _GetAppName(sig, targetRef);
					}
				}
				icon = LoadBestIconForRef(targetRef, signature.String(), 32);
			}

			if (!icon)
				icon = LoadBestIconForRef(targetRef, nullptr, 32);

			UnifiedListItem* item = new UnifiedListItem(
				name.String(),
				parentPath.Path(),
				targetRef,
				signature.String(),
				icon,
				symlinkPath.Path()
			);
			outList->AddItem(item);
		}
	}
	
	void _LoadFavorites(BList* outList) {
		if (!outList)
			return;

		
		BPath favPath;
		if (find_directory(B_USER_SETTINGS_DIRECTORY, &favPath) != B_OK)
			return;

		favPath.Append("deskbar/menu/Favorites");
		BDirectory dir(favPath.Path());
		if (dir.InitCheck() != B_OK)
			return;

		BEntry entry;
		while (dir.GetNextEntry(&entry, false) == B_OK) {
			entry_ref ref;
			if (entry.GetRef(&ref) != B_OK)
				continue;
				
			BPath symlinkPath;
			entry.GetPath(&symlinkPath);

			BEntry target(&ref, true);
			if (target.InitCheck() != B_OK || !target.Exists())
				continue;

			entry_ref targetRef;
			if (target.GetRef(&targetRef) != B_OK)
				continue;

			BPath path;
			target.GetPath(&path);
			BPath parentPath;
			path.GetParent(&parentPath);

			BNode node(&target);
			BNodeInfo nodeInfo(&node);
			char mimeType[B_MIME_TYPE_LENGTH] = {};

			BString signature;
			BString name = targetRef.name;
			BBitmap* icon = nullptr;

			if (nodeInfo.GetType(mimeType) == B_OK) {
				if (strstr(mimeType, "application/x-vnd.") == mimeType) {
					BFile f(&targetRef, B_READ_ONLY);
					BAppFileInfo appInfo(&f);
					char sig[B_MIME_TYPE_LENGTH];
					if (appInfo.GetSignature(sig) == B_OK) {
						signature = sig;
						name = _GetAppName(sig, targetRef);
					}
				}
				icon = LoadBestIconForRef(targetRef, signature.String(), 32);
			}

			if (!icon)
				icon = LoadBestIconForRef(targetRef, nullptr, 32);

			UnifiedListItem* item = new UnifiedListItem(
				name.String(),
				parentPath.Path(),
				targetRef,
				signature.String(),
				icon,
				symlinkPath.Path()
			);

			outList->AddItem(item);
		}
	}
	
	void _LoadGoTo(BList* outList) {
		if (!outList)
			return;

		BPath goPath;
		if (find_directory(B_USER_SETTINGS_DIRECTORY, &goPath) != B_OK)
			return;

		goPath.Append("Tracker/Go");
		BDirectory dir(goPath.Path());
		if (dir.InitCheck() != B_OK)
			return;

		BEntry entry;
		while (dir.GetNextEntry(&entry, false) == B_OK) {
			entry_ref ref;
			if (entry.GetRef(&ref) != B_OK)
				continue;
				
			BPath symlinkPath;
			entry.GetPath(&symlinkPath);

			BEntry target(&ref, true);
			if (target.InitCheck() != B_OK || !target.Exists())
				continue;

			entry_ref targetRef;
			if (target.GetRef(&targetRef) != B_OK)
				continue;

			BPath path;
			target.GetPath(&path);
			BPath parentPath;
			path.GetParent(&parentPath);

			BNode node(&target);
			BNodeInfo nodeInfo(&node);
			char mimeType[B_MIME_TYPE_LENGTH] = {};

			BString signature;
			BString name = targetRef.name;
			BBitmap* icon = nullptr;

			if (nodeInfo.GetType(mimeType) == B_OK) {
				if (strstr(mimeType, "application/x-vnd.") == mimeType) {
					BFile f(&targetRef, B_READ_ONLY);
					BAppFileInfo appInfo(&f);
					char sig[B_MIME_TYPE_LENGTH];
					if (appInfo.GetSignature(sig) == B_OK) {
						signature = sig;
						name = _GetAppName(sig, targetRef);
					}
				}
				icon = LoadBestIconForRef(targetRef, signature.String(), 32);
			}

			if (!icon)
				icon = LoadBestIconForRef(targetRef, nullptr, 32);

			UnifiedListItem* item = new UnifiedListItem(
				name.String(),
				parentPath.Path(),
				targetRef,
				signature.String(),
				icon,
				symlinkPath.Path()
			);

			outList->AddItem(item);
		}
	}
	
	void _OpenInfoOnSelected() {
		int32 index = fMainList->CurrentSelection();
		if (index >= 0) {
			auto* item = dynamic_cast<UnifiedListItem*>(fMainList->ItemAt(index));
			if (item) {
				BPath fullPath(&item->Ref());
				OpenInfo(fullPath.Path());
			}
		}
	}
		
	void _LaunchSelectedItem() {
		int32 index = fMainList->CurrentSelection();
		if (index >= 0) {
			if (VolumeListItem* volItem = dynamic_cast<VolumeListItem*>(fMainList->ItemAt(index))) {
				BMessage toggleMsg(kToggleVolume);
				toggleMsg.AddInt32("partition_id", volItem->PartitionId());
				toggleMsg.AddBool("mount", !volItem->IsMounted());
				PostMessage(&toggleMsg);
				return;
			}
			
			UnifiedListItem* item = dynamic_cast<UnifiedListItem*>(fMainList->ItemAt(index));
			if (item) {
				BEntry entry(&item->Ref(), true);
				if (entry.Exists()) {
					if (entry.IsDirectory()) {
						BMessenger tracker("application/x-vnd.Be-TRAK");
						if (tracker.IsValid()) {
							BMessage openMsg(B_REFS_RECEIVED);
							openMsg.AddRef("refs", &item->Ref());
							tracker.SendMessage(&openMsg);
							PostMessage(B_QUIT_REQUESTED);
						}
					} else {
						if (!item->Signature().IsEmpty()) {
							be_roster->Launch(item->Signature().String());
							PostMessage(B_QUIT_REQUESTED);
						} else {
							BNode node(&item->Ref());
							if (node.InitCheck() == B_OK) {
								BNodeInfo nodeInfo(&node);
								char mimeType[B_MIME_TYPE_LENGTH];
								if (nodeInfo.GetType(mimeType) == B_OK) {
									if (strcmp(mimeType, "application/x-vnd.Be-elfexecutable") == 0 ||
										strstr(mimeType, "application/x-vnd.") == mimeType) {
										be_roster->Launch(&item->Ref());
									} else {
										be_roster->Launch(&item->Ref());
									}
								} else {
									be_roster->Launch(&item->Ref());
								}
								PostMessage(B_QUIT_REQUESTED);
							}
						}
					}
				}
			}
		}
	}
	
	void _ShowContextMenu(BPoint where, int32 index) {
		if (VolumeListItem* volItem = dynamic_cast<VolumeListItem*>(fMainList->ItemAt(index))) {
			BPopUpMenu* menu = new BPopUpMenu("volume_context", false, false);
			
			if (volItem->IsMounted()) {
				bool isBoot = false;

				if (volItem->PartitionId() > 0) {
					BDiskDeviceList devices;
					if (devices.Fetch() == B_OK) {
						BPartition* partition = devices.PartitionWithID(volItem->PartitionId());
						if (partition) {
							BVolume volume;
							if (partition->GetVolume(&volume) == B_OK) {
								BVolume bootVolume;
								BVolumeRoster().GetBootVolume(&bootVolume);
								if (volume == bootVolume)
									isBoot = true;
							}
						}
					}
				}

				if (!isBoot) {
					menu->AddItem(new BMenuItem(B_TRANSLATE("Unmount"), new BMessage(kToggleVolume)));
				}
				
				if (volItem->PartitionId() > 0) {
					BDiskDeviceList devices;
					if (devices.Fetch() == B_OK) {
						BPartition* partition = devices.PartitionWithID(volItem->PartitionId());
						if (partition) {
							BVolume volume;
							if (partition->GetVolume(&volume) == B_OK) {
								BDirectory rootDir;
								volume.GetRootDirectory(&rootDir);
								BEntry entry;
								rootDir.GetEntry(&entry);
								entry_ref ref;
								if (entry.GetRef(&ref) == B_OK) {
									BMessage* showMsg = new BMessage(kMsgRevealInTracker);
									showMsg->AddRef("refs", &ref);
									menu->AddItem(new BMenuItem(B_TRANSLATE("Reveal in Tracker"), showMsg, 'R'));
									
									BMessage* infoMsg = new BMessage(kMsgGetInfo);
									infoMsg->AddRef("refs", &ref);
									menu->AddItem(new BMenuItem(B_TRANSLATE("Get Info"), infoMsg , 'I'));
								}
							}
						}
					}
				}
			} else {
				menu->AddItem(new BMenuItem(B_TRANSLATE("Mount"), new BMessage(kToggleVolume)));
			}
			
			for (int32 i = 0; i < menu->CountItems(); i++) {
				BMenuItem* item = menu->ItemAt(i);
				if (item->Message() && item->Message()->what == kToggleVolume) {
					item->Message()->AddInt32("partition_id", volItem->PartitionId());
					item->Message()->AddBool("mount", !volItem->IsMounted());
				}
			}
			
			menu->SetTargetForItems(this);
			menu->Go(fMainList->ConvertToScreen(where), true, false, true);
			return;
		}
		
		UnifiedListItem* item = dynamic_cast<UnifiedListItem*>(fMainList->ItemAt(index));
		if (!item) return;
		
		BPopUpMenu* menu = new BPopUpMenu("context_menu", false, false);
		
		menu->AddItem(new BMenuItem(B_TRANSLATE("Launch"), new BMessage(kMsgLaunch)));
		
		BMessage* showMsg = new BMessage(kMsgRevealInTracker);
		showMsg->AddRef("refs", &item->Ref());
		menu->AddItem(new BMenuItem(B_TRANSLATE("Reveal in Tracker"), showMsg , 'R'));
		
		BMessage* infoMsg = new BMessage(kMsgGetInfo);
		infoMsg->AddRef("refs", &item->Ref());
		menu->AddItem(new BMenuItem(B_TRANSLATE("Get Info"), infoMsg , 'I'));
		
		bool isUnrelated=false;
		if (item->IsUnrelated()) isUnrelated = true;
		
		if (fCurrentView != kShowGoTo && (isUnrelated || fCurrentView != kShowFavorites)) { //fUnrelated
			menu->AddSeparatorItem();
			
			BMessage* addFavMsg = new BMessage(kMsgAddFavorite);
			addFavMsg->AddRef("refs", &item->Ref());
			menu->AddItem(new BMenuItem(B_TRANSLATE("Add to Favorites"), addFavMsg));
			
			BEntry entry(&item->Ref(), true);
			if (entry.IsDirectory()) {
				BMessage* addGoMsg = new BMessage(kMsgAddGoTo);
				addGoMsg->AddRef("refs", &item->Ref());
				menu->AddItem(new BMenuItem(B_TRANSLATE("Add to Go To"), addGoMsg));
			}
		}
		
		if ((fCurrentView == kShowFavorites || fCurrentView == kShowGoTo) && 
			!item->SymlinkPath().IsEmpty()) {
			menu->AddSeparatorItem();
			BMessage* removeMsg = new BMessage(kMsgRemoveItem);
			removeMsg->AddString("symlink_path", item->SymlinkPath());
			removeMsg->AddInt32("index", index);
			menu->AddItem(new BMenuItem(B_TRANSLATE("Remove"), removeMsg, B_DELETE));
		}
		
		menu->SetTargetForItems(this);
		menu->Go(fMainList->ConvertToScreen(where), true, false, true);
	}
	
   
	static bool _CompareItems(const UnifiedListItem* a, const UnifiedListItem* b) {

		int cmp = strcasecmp(a->MimeType().String(), b->MimeType().String());
		if (cmp != 0)
			return cmp < 0;
		

		return strcasecmp(a->Name().String(), b->Name().String()) < 0;
	}

	void _InsertSortedItem(UnifiedListItem* item) {
		int32 insertIndex = fMainList->CountItems();
		for (int32 j = 0; j < fMainList->CountItems(); j++) {
			auto* existing = dynamic_cast<UnifiedListItem*>(fMainList->ItemAt(j));
			if (existing && _CompareItems(item, existing)) {
				insertIndex = j;
				break;
			}
		}
		fMainList->AddItem(item, insertIndex);
	}

	BString _GetSignature(const entry_ref& ref) {
		BString sig;
		BFile f(&ref, B_READ_ONLY);
		if (f.InitCheck() == B_OK) {
			BAppFileInfo afi(&f);
			char s[B_MIME_TYPE_LENGTH];
			if (afi.GetSignature(s) == B_OK)
				sig = s;
		}
		return sig;
	}
	
	BString _GetAppName(const char* signature, const entry_ref& ref) {
		// Try localized file name first
		if (BLocaleRoster::Default()->IsFilesystemTranslationPreferred()) {
			BString localizedName;
			if (BLocaleRoster::Default()->GetLocalizedFileName(
					localizedName, ref) == B_OK && localizedName.Length() > 0)
				return localizedName;
		}
		// Fall back to MIME short description, then file name
		BString label = ref.name;
		if (signature && signature[0]) {
			BMimeType mt(signature);
			if (mt.IsInstalled()) {
				char shortDesc[B_MIME_TYPE_LENGTH];
				if (mt.GetShortDescription(shortDesc) == B_OK && shortDesc[0] != '\0')
					label = shortDesc;
			}
		}
		return label;
	}
	
	void _RevealInTrackerOnSelected() {
		int32 index = fMainList->CurrentSelection();
		if (index >= 0) {
			auto* item = dynamic_cast<UnifiedListItem*>(fMainList->ItemAt(index));
			if (item) {
				RevealInTracker(item->Ref());
			}
		}
	}
	
	static int32 _MountAllWorker(void* data) {
		DeskbarWindow* win = reinterpret_cast<DeskbarWindow*>(data);
		
		BDiskDeviceList devices;
		if (devices.Fetch() == B_OK) {
			class MountAllVisitor : public BDiskDeviceVisitor {
			public:
				using BDiskDeviceVisitor::Visit;
				DeskbarWindow* fWin;
				MountAllVisitor(DeskbarWindow* w) : fWin(w) {}
				virtual bool Visit(BPartition* partition, int32) {
					if (partition->ContainsFileSystem() && !partition->IsMounted()) {
						partition->Mount();
					}
					return false;
				}
			} visitor(win);
			devices.VisitEachPartition(&visitor);
		}


		BMessenger(win).SendMessage(kShowVolumes);
		return 0;
	}

	
	static int32 _MountWorker(void* data) {
		struct MountData {
			int32 partitionId;
			bool mount;
		};
		
		MountData* mountData = (MountData*)data;
		
		BDiskDeviceList devices;
		if (devices.Fetch() == B_OK) {
			BPartition* partition = devices.PartitionWithID(mountData->partitionId);
			if (partition) {
				if (mountData->mount) {
					partition->Mount();
				} else {
					partition->Unmount();
				}
			}
		}
		
		delete mountData;
		return 0;
	}
	
	static int32 _UnmountAllWorker(void* data) {
		DeskbarWindow* win = reinterpret_cast<DeskbarWindow*>(data);

		BDiskDeviceList devices;
		if (devices.Fetch() == B_OK) {
			class UnmountAllVisitor : public BDiskDeviceVisitor {
			public:
				using BDiskDeviceVisitor::Visit;
				virtual bool Visit(BPartition* partition, int32) {
					if (partition->ContainsFileSystem() && partition->IsMounted()) {
						BVolume volume;
						if (partition->GetVolume(&volume) == B_OK) {
							BVolume bootVolume;
							BVolumeRoster().GetBootVolume(&bootVolume);
							if (volume != bootVolume) {
								partition->Unmount();
							}
						}
					}
					return false;
				}
			} visitor;
			devices.VisitEachPartition(&visitor);
		}

		BVolumeRoster volumeRoster;
		BVolume volume;
		while (volumeRoster.GetNextVolume(&volume) == B_OK) {
			if (volume.IsShared()) {
				BMessenger tracker("application/x-vnd.Be-TRAK");
				BMessage unmountMsg('Tunm');

				BDirectory rootDir;
				volume.GetRootDirectory(&rootDir);
				BEntry entry;
				rootDir.GetEntry(&entry);
				entry_ref ref;
				entry.GetRef(&ref);

				unmountMsg.AddRef("refs", &ref);
				tracker.SendMessage(&unmountMsg);
			}
		}

		if (win) {
			BMessenger(win).SendMessage(kShowVolumes);
		}

		return 0;
	}
	
	static int32 _FindWorker(void* data) {
		struct Params {
			DeskbarWindow* win;
			BString filter;
		};
		Params* p = reinterpret_cast<Params*>(data);
		DeskbarWindow* window = p->win;
		BString filter = p->filter;
		delete p;

		BString pattern = BuildCaseInsensitivePattern(filter, true);

		BVolumeRoster roster;
		BVolume volume;
		while (roster.GetNextVolume(&volume) == B_OK) {
			if (!volume.IsPersistent() || !volume.KnowsQuery())
				continue;

			if (window->fStopFind.load())
				return 0;

			BQuery query;
			query.SetVolume(&volume);

			BString predicate;
			predicate.SetToFormat("name=%s", pattern.String());
			query.SetPredicate(predicate.String());

			if (query.Fetch() == B_OK) {
				entry_ref ref;
				BMessage batch(kFindResults);

				while (query.GetNextRef(&ref) == B_OK) {
					if (window->fStopFind.load())
						return 0;

					batch.AddRef("refs", &ref);

					if (batch.CountNames(B_REF_TYPE) >= 10) {
						window->PostMessage(&batch);
						batch.MakeEmpty();
						batch.what = kFindResults;
					}
				}

				if (batch.CountNames(B_REF_TYPE) > 0)
					window->PostMessage(&batch);
			}
		}

		window->PostMessage(kFindDone);
		return 0;
	}
	
	void DispatchMessage(BMessage* message, BHandler* target) override {
		if (message->what == B_KEY_DOWN) {
			const char* bytes;
			if (message->FindString("bytes", &bytes) == B_OK) {
				if (fFilter->TextView()->IsFocus()) {
					bool handled = false;

					switch (bytes[0]) {
						case B_ENTER: {
							int32 selected = fMainList->CurrentSelection();
							if (selected < 0 && fMainList->CountItems() > 0)
								selected = 0;

							if (selected >= 0) {
								fMainList->Select(selected);
								_LaunchSelectedItem();
							}
							handled = true;
							break;
						}

						case B_DOWN_ARROW: {
							int32 current = fMainList->CurrentSelection();
							int32 count = fMainList->CountItems();

							if (count > 0) {
								if (current < 0)
									fMainList->Select(0);
								else if (current < count - 1)
									fMainList->Select(current + 1);

								fMainList->ScrollToSelection();
							}
							handled = true;
							break;
						}

						case B_UP_ARROW: {
							int32 current = fMainList->CurrentSelection();
							if (current > 0) {
								fMainList->Select(current - 1);
								fMainList->ScrollToSelection();
							}
							handled = true;
							break;
						}
						
						case B_PAGE_UP:
						{
							if (fMainList->CountItems() > 0) {
								fMainList->Select(0);
								fMainList->ScrollToSelection();
							}
							handled = true;
							break;
						}
						
						case B_PAGE_DOWN:
						{
							int32 count = fMainList->CountItems();
							if (count > 0) {
								fMainList->Select(count - 1);
								fMainList->ScrollToSelection();
							}
							handled = true;
							break;
						}

						case B_ESCAPE: {
							fFilter->SetText("");
							_ApplyFilter("");
							handled = true;
							break;
						}

						case B_FUNCTION_KEY:
						{
							int32 key;
							if (message->FindInt32("key", &key) == B_OK && key == B_F10_KEY) {
								int32 selected = fMainList->CurrentSelection();
								if (selected >= 0) {
									BRect itemRect = fMainList->ItemFrame(selected);
									BPoint where(itemRect.left + 10, itemRect.bottom);

									BMessage contextMsg(kMsgOpenMenu);
									contextMsg.AddPoint("where", where);
									contextMsg.AddInt32("index", selected);
									PostMessage(&contextMsg);
								}
								handled = true;
								break;
							}
						}
					}

					if (handled) 
						return;
				}
			}
		}
		
		int32 modifiers = 0;
		if (message->FindInt32("modifiers", &modifiers) == B_OK) {
			const char* bytes;
			if (message->FindString("bytes", &bytes) == B_OK) {
				if ((modifiers & B_COMMAND_KEY) != 0) {
					switch (bytes[0]) {
						case 'd': case 'D':
							PostMessage(kShowVolumes);
							return;
							
						case 'a': case 'A':
							PostMessage(kShowApps);
							return;
							
						case 'g': case 'G':
							PostMessage(kShowGoTo);
							return;
							
						case 's': case 'S':
							PostMessage(kShowFavorites);
							return;
						
						case 'p': case 'P':
							PostMessage(kShowPrefs);
							return;

						case 'f': case 'F':
							PostMessage(kShowFind);
							return;
							
						case 'j': case 'J':
							PostMessage(kShowRecentApps);
							return;
							
						case 'k': case 'K':
							PostMessage(kShowRecentDocs);
							return;
							
						case 'l': case 'L':
							PostMessage(kShowRecentFolders);
							return;
						
						case 'i': case 'I':
							_OpenInfoOnSelected();
							return;

						case 'r': case 'R':
							_RevealInTrackerOnSelected();
							return;
					}
				}
			}
		}

		BWindow::DispatchMessage(message, target);
	}
};

// -------------------------------------------------------------
// App
// -------------------------------------------------------------

class DeskbarApp : public BApplication {
public:
	DeskbarApp() : BApplication("application/x-vnd.SpielBar") {}

	void ArgvReceived(int32 argc, char** argv) override {
		if (argc > 1) {
			fStartArg = argv[1];
		}
	}

	void ReadyToRun() override {
		DeskbarWindow* win = new DeskbarWindow();
		win->Show();

		if (!fStartArg.IsEmpty()) {
			_HandleStartArg(win, fStartArg);
		}
	}

private:
	BString fStartArg;

	void _HandleStartArg(DeskbarWindow* win, const BString& arg) {
		BMessage msg;
		if (arg.ICompare("--apps") == 0) {
			msg.what = kShowApps;
		} else if (arg.ICompare("--find") == 0) {
			msg.what = kShowFind;
		} else if (arg.ICompare("--recent-apps") == 0) {
			msg.what = kShowRecentApps;
		} else if (arg.ICompare("--recent-docs") == 0) {
			msg.what = kShowRecentDocs;
		} else if (arg.ICompare("--recent-folders") == 0) {
			msg.what = kShowRecentFolders;
		} else if (arg.ICompare("--prefs") == 0) {
			msg.what = kShowPrefs;
		} else if (arg.ICompare("--favorites") == 0) {
			msg.what = kShowFavorites;
		} else if (arg.ICompare("--goto") == 0) {
			msg.what = kShowGoTo;
		} else if (arg.ICompare("--volumes") == 0) {
			msg.what = kShowVolumes;
		} else {
			return;
		}
		win->PostMessage(&msg);
	}
};

int main() {
	DeskbarApp app;
	app.Run();
	return B_OK;
}
