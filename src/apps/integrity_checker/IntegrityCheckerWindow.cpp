#include "IntegrityCheckerWindow.h"
#include "ResultsListView.h"
#include "QueryBuilderView.h"
#include "SettingsView.h"
#include "ScanThread.h"
#include "RepairThread.h"
#include "../../bin/fsck_hpkgs/IntegrityQuery.h"

#include <Alert.h>
#include <Application.h>
#include <Box.h>
#include <Button.h>
#include <Directory.h>
#include <File.h>
#include <FindDirectory.h>
#include <GroupLayout.h>
#include <GroupLayoutBuilder.h>
#include <LayoutItem.h>
#include <Path.h>
#include <Rect.h>
#include <ScrollView.h>
#include <SpaceLayoutItem.h>
#include <StatusBar.h>
#include <StringView.h>
#include <TabView.h>
#include <Menu.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <stdio.h>
#include <string.h>

static const char* kSettingsFileName = "integrity_checker_settings";

IntegrityCheckerWindow::IntegrityCheckerWindow()
	: BWindow(BRect(100, 100, 900, 700), "HPKG Integrity Checker",
		B_TITLED_WINDOW, B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS),
	  fTabView(NULL),
	  fResultsView(NULL),
	  fProgressBar(NULL),
	  fScanButton(NULL),
	  fRepairButton(NULL),
	  fQueryView(NULL),
	  fQueryButton(NULL),
	  fSettingsView(NULL),
	  fSaveSettingsButton(NULL),
	  fScanThread(NULL),
	  fScanInProgress(false),
	  fRepairThread(NULL)
{
	_CreateUI();
	_LoadSettings();
	CenterOnScreen();
}

IntegrityCheckerWindow::~IntegrityCheckerWindow()
{
	if (fScanThread) {
		fScanThread->SetWindow(NULL);
		fScanThread->RequestQuit();
	}
	if (fRepairThread)
		fRepairThread->SetWindow(NULL);
}

BView*
IntegrityCheckerWindow::_CreatePanelView(const char* name, BLayout* layout)
{
	// A plain BView created with no flags/no explicit color paints
	// whatever the undefined default happens to be (white, on this
	// build) instead of the normal panel background - give every
	// container view in this window a real background explicitly.
	BView* view = new BView(name, B_WILL_DRAW, layout);
	view->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	return view;
}

void
IntegrityCheckerWindow::_CreateUI()
{
	// Create main tab view
	fTabView = new BTabView("main_tabs", B_WIDTH_FROM_LABEL);

	_CreateScanTab();
	_CreateQueryTab();
	_CreateSettingsTab();

	// Create main layout
	BGroupLayout* layout = new BGroupLayout(B_VERTICAL, 5);
	SetLayout(layout);
	layout->AddView(fTabView);
	layout->SetInsets(5, 5, 5, 5);
}

void
IntegrityCheckerWindow::_CreateScanTab()
{
	// Create scan results tab
	BGroupLayout* tabLayout = new BGroupLayout(B_VERTICAL, 5);
	BView* tabView = _CreatePanelView("scan_tab", tabLayout);
	tabLayout->SetInsets(10, 10, 10, 10);

	// Results list view
	fResultsView = new ResultsListView("results", B_SINGLE_SELECTION_LIST);
	fResultsView->SetExplicitMinSize(BSize(750, 350));

	BScrollView* scrollView = new BScrollView("results_scroll",
		fResultsView, B_WILL_DRAW | B_FRAME_EVENTS, false, true);

	// Progress bar
	fProgressBar = new BStatusBar("progress", "Scan Progress");
	fProgressBar->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, 20));

	// Buttons
	fScanButton = new BButton("scan_btn", "Scan All Packages",
		new BMessage(MSG_SCAN_START));
	fScanButton->SetTarget(this);
	fRepairButton = new BButton("repair_btn", "Auto-Repair Selected",
		new BMessage(MSG_PACKAGE_REPAIR));
	fRepairButton->SetTarget(this);
	fRepairButton->SetEnabled(false);

	BGroupLayout* buttonLayout = new BGroupLayout(B_HORIZONTAL, 5);
	buttonLayout->AddView(fScanButton);
	buttonLayout->AddView(fRepairButton);
	buttonLayout->AddItem(BSpaceLayoutItem::CreateGlue());

	BView* buttonView = _CreatePanelView("buttons", buttonLayout);
	buttonLayout->SetInsets(0, 0, 0, 0);

	// Add to tab layout
	tabLayout->AddView(new BStringView(NULL, "Scan Status:"));
	tabLayout->AddView(scrollView);
	tabLayout->AddView(new BStringView(NULL, "Progress:"));
	tabLayout->AddView(fProgressBar);
	tabLayout->AddView(buttonView);

	fTabView->AddTab(tabView);
	fTabView->TabAt(0)->SetLabel("Scan Results");
}

void
IntegrityCheckerWindow::_CreateQueryTab()
{
	// Create query builder tab
	BGroupLayout* tabLayout = new BGroupLayout(B_VERTICAL, 5);
	BView* tabView = _CreatePanelView("query_tab", tabLayout);
	tabLayout->SetInsets(10, 10, 10, 10);

	// Query builder view
	fQueryView = new QueryBuilderView("query", B_SINGLE_SELECTION_LIST);
	fQueryView->SetExplicitMinSize(BSize(750, 400));

	BScrollView* scrollView = new BScrollView("query_scroll",
		fQueryView, B_WILL_DRAW | B_FRAME_EVENTS, false, true);

	// Query button
	fQueryButton = new BButton("query_btn", "Execute Query",
		new BMessage(MSG_QUERY_EXECUTE));
	fQueryButton->SetTarget(this);

	BGroupLayout* buttonLayout = new BGroupLayout(B_HORIZONTAL, 5);
	buttonLayout->AddView(fQueryButton);
	buttonLayout->AddItem(BSpaceLayoutItem::CreateGlue());

	BView* buttonView = _CreatePanelView("buttons", buttonLayout);
	buttonLayout->SetInsets(0, 0, 0, 0);

	// Add to tab layout
	tabLayout->AddView(new BStringView(NULL,
		"Select a query below, then Execute Query - results appear in "
		"the Scan Results tab:"));
	tabLayout->AddView(scrollView);
	tabLayout->AddView(buttonView);

	fTabView->AddTab(tabView);
	fTabView->TabAt(1)->SetLabel("Query Builder");
}

void
IntegrityCheckerWindow::_CreateSettingsTab()
{
	// Create settings tab
	BGroupLayout* tabLayout = new BGroupLayout(B_VERTICAL, 5);
	BView* tabView = _CreatePanelView("settings_tab", tabLayout);
	tabLayout->SetInsets(10, 10, 10, 10);

	// Settings view
	fSettingsView = new SettingsView("settings");
	fSettingsView->SetExplicitMinSize(BSize(700, 350));

	// Save settings button
	fSaveSettingsButton = new BButton("save_btn", "Save Settings",
		new BMessage(MSG_SAVE_SETTINGS));
	fSaveSettingsButton->SetTarget(this);

	BGroupLayout* buttonLayout = new BGroupLayout(B_HORIZONTAL, 5);
	buttonLayout->AddView(fSaveSettingsButton);
	buttonLayout->AddItem(BSpaceLayoutItem::CreateGlue());

	BView* buttonView = _CreatePanelView("buttons", buttonLayout);
	buttonLayout->SetInsets(0, 0, 0, 0);

	// Add to tab layout
	tabLayout->AddView(new BStringView(NULL, "Preferences:"));
	tabLayout->AddView(fSettingsView);
	tabLayout->AddView(buttonView);

	fTabView->AddTab(tabView);
	fTabView->TabAt(2)->SetLabel("Settings");
}

void
IntegrityCheckerWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case MSG_SCAN_START:
			_StartScan();
			break;
		
		case MSG_SCAN_COMPLETE:
			_ScanComplete(message);
			break;
		
		case MSG_SCAN_PROGRESS:
		{
			int current = message->FindInt32("current");
			int total = message->FindInt32("total");
			_UpdateProgress(current, total);
			break;
		}
		
		case MSG_QUERY_EXECUTE:
			_ExecuteQuery();
			break;

		case MSG_SAVE_SETTINGS:
			_SaveSettings();
			break;
		
		case MSG_PACKAGE_REPAIR:
			_HandlePackageRepair();
			break;

		case MSG_REPAIR_COMPLETE:
			_RepairComplete(message);
			break;

		case MSG_PACKAGE_LOCATE:
		{
			BString packageName;
			if (message->FindString("package", &packageName) == B_OK) {
				_HandlePackageLocate(packageName.String());
			}
			break;
		}

		default:
			BWindow::MessageReceived(message);
			break;
	}
}

bool
IntegrityCheckerWindow::QuitRequested()
{
	_StopScan();
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}

void
IntegrityCheckerWindow::_StartScan()
{
	if (fScanInProgress)
		return;
	
	fScanInProgress = true;
	fScanButton->SetEnabled(false);
	fProgressBar->Reset();
	fProgressBar->SetText("Scanning packages...");
	fResultsView->MakeEmpty();
	
	// Create and start scan thread
	fScanThread = new ScanThread(this);
	fScanThread->Run();
}

void
IntegrityCheckerWindow::_StopScan()
{
	if (fScanThread && fScanThread->IsRunning()) {
		fScanThread->RequestQuit();
	}
}

void
IntegrityCheckerWindow::_UpdateProgress(int current, int total)
{
	if (total > 0) {
		char buffer[64];
		snprintf(buffer, sizeof(buffer), "%d/%d files", current, total);
		fProgressBar->Update(current - fProgressBar->CurrentValue(), buffer);
	}
}

void
IntegrityCheckerWindow::_ScanComplete(BMessage* results)
{
	fScanInProgress = false;
	fScanButton->SetEnabled(true);
	fProgressBar->SetText("Scan complete!");

	fResultsView->MakeEmpty();

	BMessage packageList;
	results->FindMessage("results", &packageList);

	int32 needsAttention = 0;

	for (int32 i = 0; ; i++) {
		BMessage packageMessage;
		if (packageList.FindMessage("package", i, &packageMessage) != B_OK)
			break;

		ResultsListView::PackageInfo info;
		packageMessage.FindString("name", &info.name);
		packageMessage.FindString("path", &info.packagePath);
		packageMessage.FindString("status", &info.status);
		packageMessage.FindString("checksum_expected", &info.checksum);
		packageMessage.FindString("checksum_actual", &info.actualChecksum);
		packageMessage.FindString("source", &info.source);

		fResultsView->AddPackageResult(info);

		if (info.status != "ok")
			needsAttention++;
	}

	fRepairButton->SetEnabled(needsAttention > 0);
}

void
IntegrityCheckerWindow::_ExecuteQuery()
{
	const char* filter = fQueryView->GetStatusFilter();

	IntegrityQuery query;
	if (filter != NULL) {
		if (strcmp(filter, "corrupted") == 0)
			query.QueryCorrupted();
		else if (strcmp(filter, "unverified") == 0)
			query.QueryUnverified();
		else if (strcmp(filter, "ok") == 0)
			query.QueryOK();
		// NULL (e.g. "Show all packages") leaves the predicate empty,
		// which IntegrityQuery treats as "list everything".
	}

	status_t status = query.Execute("/boot/system/packages");
	if (status != B_OK) {
		BString message;
		message.SetToFormat("Query failed: %s", strerror(status));
		(new BAlert("Query failed", message.String(), "OK"))->Go();
		return;
	}

	fResultsView->MakeEmpty();

	int32 needsAttention = 0;
	for (int32 i = 0; i < query.CountResults(); i++) {
		PackageQueryResult result;
		if (query.GetResult(i, result) != B_OK)
			continue;

		ResultsListView::PackageInfo info;
		info.name = result.filename;
		info.packagePath = result.path;
		info.status = result.status;
		info.checksum = result.checksum;

		fResultsView->AddPackageResult(info);

		if (info.status != "ok")
			needsAttention++;
	}

	fRepairButton->SetEnabled(needsAttention > 0);

	// Query results are shown in the same list the scan uses - switch
	// there so the user actually sees them.
	fTabView->Select(0);
}

void
IntegrityCheckerWindow::_SaveSettings()
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK)
		return;
	path.Append(kSettingsFileName);

	BMessage settings('IChS');
	settings.AddBool("auto_repair", fSettingsView->IsAutoRepairEnabled());
	settings.AddBool("notifications", fSettingsView->IsNotificationsEnabled());
	settings.AddBool("logging", fSettingsView->IsLoggingEnabled());
	settings.AddString("log_path", fSettingsView->GetLogPath());

	BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (file.InitCheck() != B_OK || settings.Flatten(&file) != B_OK) {
		(new BAlert("Settings", "Failed to save settings.", "OK"))->Go();
		return;
	}

	(new BAlert("Settings", "Settings saved.", "OK"))->Go();
}

void
IntegrityCheckerWindow::_LoadSettings()
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK)
		return;
	path.Append(kSettingsFileName);

	BFile file(path.Path(), B_READ_ONLY);
	if (file.InitCheck() != B_OK)
		return;

	BMessage settings;
	if (settings.Unflatten(&file) != B_OK)
		return;

	bool autoRepair = true, notifications = true, logging = true;
	BString logPath;
	settings.FindBool("auto_repair", &autoRepair);
	settings.FindBool("notifications", &notifications);
	settings.FindBool("logging", &logging);
	settings.FindString("log_path", &logPath);

	fSettingsView->SetAutoRepair(autoRepair);
	fSettingsView->SetNotifications(notifications);
	fSettingsView->SetLogging(logging);
	if (!logPath.IsEmpty())
		fSettingsView->SetLogPath(logPath.String());
}

void
IntegrityCheckerWindow::_HandlePackageRepair()
{
	ResultsListView::PackageInfo info;
	if (!fResultsView->GetSelectedPackageInfo(info)) {
		(new BAlert("No selection",
			"Select a package in the list first.", "OK"))->Go();
		return;
	}

	if (info.status == "ok") {
		BString message;
		message.SetToFormat("%s is already OK - nothing to repair.",
			info.name.String());
		(new BAlert("Nothing to repair", message.String(), "OK"))->Go();
		return;
	}

	if (fRepairThread != NULL) {
		(new BAlert("Repair in progress",
			"A repair is already running - wait for it to finish first.",
			"OK"))->Go();
		return;
	}

	// This never happens without the user explicitly saying yes here -
	// no auto-repair runs on its own.
	BString prompt;
	prompt.SetToFormat(
		"Repair %s now?\n\n"
		"This downloads (or uses the file you provide) a verified copy "
		"and overwrites this package's content in place. A REBOOT is "
		"required afterwards for the fix to take effect.",
		info.name.String());

	BAlert* alert = new BAlert("Confirm repair", prompt.String(),
		"Cancel", "Repair", NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
	alert->SetShortcut(0, B_ESCAPE);
	int32 choice = alert->Go();
	if (choice != 1)
		return;

	fRepairButton->SetEnabled(false);
	fRepairThread = new RepairThread(this, info);
	fRepairThread->Run();
}

void
IntegrityCheckerWindow::_RepairComplete(BMessage* message)
{
	fRepairThread = NULL;
	fRepairButton->SetEnabled(true);

	bool success = false;
	message->FindBool("success", &success);

	BString statusMessage;
	message->FindString("message", &statusMessage);

	(new BAlert(success ? "Repair complete" : "Repair failed",
		statusMessage.String(), "OK", NULL, NULL, B_WIDTH_AS_USUAL,
		success ? B_INFO_ALERT : B_WARNING_ALERT))->Go();

	// Re-scan so the list reflects the (pre-reboot) repaired state.
	_StartScan();
}

void
IntegrityCheckerWindow::_HandlePackageLocate(const char* packageName)
{
	// TODO: Show file browser for package
	printf("Locating: %s\n", packageName);
}
