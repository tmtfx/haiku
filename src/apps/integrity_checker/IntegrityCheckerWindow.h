#ifndef _INTEGRITY_CHECKER_WINDOW_H
#define _INTEGRITY_CHECKER_WINDOW_H

#include <Window.h>
#include <String.h>
#include <TabView.h>
#include <Button.h>
#include <StatusBar.h>

class ResultsListView;
class QueryBuilderView;
class SettingsView;
class ScanThread;
class RepairThread;
class BGroupLayout;
class BGroupView;

// Message codes
#define MSG_TAB_CHANGED 'TbCh'
#define MSG_SCAN_START 'ScSt'
#define MSG_SCAN_STOP 'ScSp'
#define MSG_SCAN_PROGRESS 'ScPr'
#define MSG_SCAN_COMPLETE 'ScCo'
#define MSG_QUERY_EXECUTE 'QxEx'
#define MSG_PACKAGE_REPAIR 'PkRp'
#define MSG_PACKAGE_LOCATE 'PkLc'
#define MSG_REPAIR_COMPLETE 'RpCo'
#define MSG_SAVE_SETTINGS 'SvSt'

class IntegrityCheckerWindow : public BWindow {
public:
	IntegrityCheckerWindow();
	virtual ~IntegrityCheckerWindow();

	virtual void MessageReceived(BMessage* message);
	virtual bool QuitRequested();

private:
	BTabView* fTabView;

	// Tab 0: Scan Results
	ResultsListView* fResultsView;
	BStatusBar* fProgressBar;
	BButton* fScanButton;
	BButton* fRepairButton;

	// Tab 1: Query Builder
	QueryBuilderView* fQueryView;
	BButton* fQueryButton;

	// Tab 2: Settings
	SettingsView* fSettingsView;
	BButton* fSaveSettingsButton;

	// Background scanning
	ScanThread* fScanThread;
	bool fScanInProgress;

	// Background repair
	RepairThread* fRepairThread;

	void _CreateUI();
	void _CreateScanTab();
	void _CreateQueryTab();
	void _CreateSettingsTab();
	static BGroupView* _CreatePanelView(const char* name, orientation dir,
		float spacing = 5.0f);

	void _StartScan();
	void _StopScan();
	void _UpdateProgress(int current, int total);
	void _ScanComplete(BMessage* results);

	void _ExecuteQuery();
	void _SaveSettings();
	void _LoadSettings();

	void _HandlePackageRepair();
	void _RepairComplete(BMessage* message);
	void _HandlePackageLocate(const char* packageName);
};

#endif // _INTEGRITY_CHECKER_WINDOW_H
