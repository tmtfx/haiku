#include "IntegrityCheckerWindow.h"
#include "ResultsListView.h"
#include "QueryBuilderView.h"
#include "SettingsView.h"
#include "ScanThread.h"
#include "RepairThread.h"

#include <Alert.h>
#include <Application.h>
#include <Box.h>
#include <Button.h>
#include <GroupLayout.h>
#include <GroupLayoutBuilder.h>
#include <LayoutItem.h>
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
	BView* tabView = new BView("scan_tab", 0, tabLayout);
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
	fRepairButton = new BButton("repair_btn", "Auto-Repair Selected",
		new BMessage(MSG_PACKAGE_REPAIR));
	fRepairButton->SetEnabled(false);
	
	BButton* queryButton = new BButton("query_btn", "Open Query Builder",
		new BMessage(B_SIMPLE_DATA));
	
	BGroupLayout* buttonLayout = new BGroupLayout(B_HORIZONTAL, 5);
	buttonLayout->AddView(fScanButton);
	buttonLayout->AddView(fRepairButton);
	buttonLayout->AddView(queryButton);
	buttonLayout->AddItem(BSpaceLayoutItem::CreateGlue());
	
	BView* buttonView = new BView("buttons", 0, buttonLayout);
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
	BView* tabView = new BView("query_tab", 0, tabLayout);
	tabLayout->SetInsets(10, 10, 10, 10);
	
	// Query builder view
	fQueryView = new QueryBuilderView("query", B_SINGLE_SELECTION_LIST);
	fQueryView->SetExplicitMinSize(BSize(750, 400));
	
	BScrollView* scrollView = new BScrollView("query_scroll",
		fQueryView, B_WILL_DRAW | B_FRAME_EVENTS, false, true);
	
	// Query button
	fQueryButton = new BButton("query_btn", "Execute Query",
		new BMessage(MSG_QUERY_EXECUTE));
	
	BGroupLayout* buttonLayout = new BGroupLayout(B_HORIZONTAL, 5);
	buttonLayout->AddView(fQueryButton);
	buttonLayout->AddItem(BSpaceLayoutItem::CreateGlue());
	
	BView* buttonView = new BView("buttons", 0, buttonLayout);
	buttonLayout->SetInsets(0, 0, 0, 0);
	
	// Add to tab layout
	tabLayout->AddView(new BStringView(NULL, "Filter Packages:"));
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
	BView* tabView = new BView("settings_tab", 0, tabLayout);
	tabLayout->SetInsets(10, 10, 10, 10);
	
	// Settings view
	fSettingsView = new SettingsView("settings");
	fSettingsView->SetExplicitMinSize(BSize(700, 350));
	
	// Save settings button
	fSaveSettingsButton = new BButton("save_btn", "Save Settings",
		new BMessage(B_SIMPLE_DATA));
	
	BGroupLayout* buttonLayout = new BGroupLayout(B_HORIZONTAL, 5);
	buttonLayout->AddView(fSaveSettingsButton);
	buttonLayout->AddItem(BSpaceLayoutItem::CreateGlue());
	
	BView* buttonView = new BView("buttons", 0, buttonLayout);
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
	// TODO: Execute query from QueryBuilderView
}

void
IntegrityCheckerWindow::_SaveSettings()
{
	// TODO: Save settings from SettingsView
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
