#include "RepairThread.h"
#include "IntegrityCheckerWindow.h"
#include "../../servers/package/RecoveryStrategy.h"
#include "../../servers/package/RepositoryRecovery.h"
#include "../../servers/package/ManualRecovery.h"

#include <Message.h>
#include <Messenger.h>
#include <String.h>
#include <time.h>

RepairThread::RepairThread(IntegrityCheckerWindow* window,
	const ResultsListView::PackageInfo& info)
	: fWindow(window),
	  fInfo(info),
	  fThread(-1)
{
}

RepairThread::~RepairThread()
{
	if (fThread >= 0) {
		status_t result;
		wait_for_thread(fThread, &result);
	}
}

status_t
RepairThread::Run()
{
	fThread = spawn_thread(_ThreadEntry, "RepairThread", B_NORMAL_PRIORITY,
		this);
	if (fThread < 0)
		return fThread;

	return resume_thread(fThread);
}

status_t
RepairThread::_ThreadEntry(void* data)
{
	((RepairThread*)data)->_Execute();
	return B_OK;
}

void
RepairThread::_Execute()
{
	PackageCorruptionInfo corruption;
	corruption.packagePath = fInfo.packagePath;
	corruption.packageName = fInfo.name;
	corruption.expectedChecksum = fInfo.checksum;
	corruption.actualChecksum = fInfo.actualChecksum;
	corruption.corruptionDetectedTime = time(NULL);
	corruption.sourceType = fInfo.source.IsEmpty()
		? RECOVERY_SOURCE_UNKNOWN
		: HPKGParseRecoverySource(fInfo.source.String());
	corruption.sourceInfo = fInfo.source;

	FileRecoveryLogger logger;
	SilentRecoveryNotifier notifier;

	RecoveryCoordinator coordinator;
	coordinator.AddStrategy(new RepositoryRecovery(&logger, &notifier));
	coordinator.AddStrategy(new ManualRecovery(&logger, &notifier));

	RecoveryResult result;
	status_t status = coordinator.AttemptRecovery(corruption, result);

	BMessage complete(MSG_REPAIR_COMPLETE);
	complete.AddBool("success", status == B_OK);
	complete.AddString("message", result.statusMessage.IsEmpty()
		? BString("Repair failed") : result.statusMessage);

	if (fWindow != NULL) {
		BMessenger messenger(fWindow);
		messenger.SendMessage(&complete);
	}
}
