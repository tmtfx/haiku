#ifndef _REPAIR_THREAD_H
#define _REPAIR_THREAD_H

#include <OS.h>

#include "ResultsListView.h"

class IntegrityCheckerWindow;

// Runs one RecoveryCoordinator::AttemptRecovery() call on a background
// kernel thread (it can involve a network download via `pkgman
// download`, so it must not block the window's own message loop), then
// posts the result back to the window. See ScanThread for the same
// pattern applied to scanning.
class RepairThread {
public:
	RepairThread(IntegrityCheckerWindow* window,
		const ResultsListView::PackageInfo& info);
	~RepairThread();

	status_t Run();

	void SetWindow(IntegrityCheckerWindow* window) { fWindow = window; }

private:
	static status_t _ThreadEntry(void* data);
	void _Execute();

	IntegrityCheckerWindow* fWindow;
	ResultsListView::PackageInfo fInfo;
	thread_id fThread;
};

#endif // _REPAIR_THREAD_H
