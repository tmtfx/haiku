#include "ScanThread.h"
#include "IntegrityCheckerWindow.h"
#include "../../bin/fsck_hpkgs/PackageVerifier.h"

#include <Directory.h>
#include <Entry.h>
#include <Message.h>
#include <Messenger.h>
#include <OS.h>
#include <Path.h>
#include <String.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

using namespace BPackageKit::BPrivate;

static const char* kPackagesPath = "/boot/system/packages";

ScanThread::ScanThread(IntegrityCheckerWindow* window)
	: fWindow(window),
	  fThread(-1),
	  fQuitRequested(false)
{
}

ScanThread::~ScanThread()
{
	if (fThread >= 0) {
		status_t result;
		wait_for_thread(fThread, &result);
	}
}

status_t
ScanThread::Run()
{
	fThread = spawn_thread(_ThreadEntry, "ScanThread", B_NORMAL_PRIORITY, this);
	if (fThread < 0)
		return fThread;

	return resume_thread(fThread);
}

bool
ScanThread::IsRunning() const
{
	if (fThread < 0)
		return false;

	thread_info info;
	return get_thread_info(fThread, &info) == B_OK;
}

status_t
ScanThread::_ThreadEntry(void* data)
{
	return ((ScanThread*)data)->_Execute();
}

status_t
ScanThread::_Execute()
{
	BMessage results;
	status_t status = _RunScan(&results);
	_SendComplete(&results);
	return status;
}

status_t
ScanThread::_RunScan(BMessage* outResults)
{
	if (!outResults)
		return B_BAD_VALUE;
	
	// Execute fsck_hpkgs with verbose output
	status_t status = _ExecuteFsckHpkgs(outResults);
	
	return status;
}

status_t
ScanThread::_ExecuteFsckHpkgs(BMessage* outResults)
{
	BDirectory directory(kPackagesPath);
	status_t status = directory.InitCheck();
	if (status != B_OK)
		return status;

	// Count .hpkg entries first, just so the progress bar has a real
	// total instead of growing unboundedly.
	int32 total = 0;
	entry_ref ref;
	while (directory.GetNextRef(&ref) == B_OK) {
		BString name(ref.name);
		if (name.EndsWith(".hpkg"))
			total++;
	}
	directory.Rewind();

	PackageVerifier verifier;
	int32 current = 0;
	int32 okCount = 0;
	int32 corruptedCount = 0;
	int32 unverifiedCount = 0;

	while (directory.GetNextRef(&ref) == B_OK) {
		if (fQuitRequested)
			return B_INTERRUPTED;

		BString name(ref.name);
		if (!name.EndsWith(".hpkg"))
			continue;

		current++;
		_SendProgress(current, total);

		BPath path(&ref);

		PackageVerificationResult result;
		verifier.VerifyPackage(path.Path(), &result);

		BMessage packageInfo;
		packageInfo.AddString("name", result.filename);
		packageInfo.AddString("path", result.filePath);
		packageInfo.AddString("checksum_expected", result.attributedChecksum);
		packageInfo.AddString("checksum_actual", result.calculatedChecksum);
		packageInfo.AddString("source", result.attributedSource);

		if (!result.attributeExists) {
			packageInfo.AddString("status", "unverified");
			unverifiedCount++;
		} else if (!result.isValid) {
			packageInfo.AddString("status", "corrupted");
			corruptedCount++;
		} else {
			packageInfo.AddString("status", "ok");
			okCount++;
		}

		outResults->AddMessage("package", &packageInfo);
	}

	outResults->AddInt32("total", current);
	outResults->AddInt32("ok", okCount);
	outResults->AddInt32("corrupted", corruptedCount);
	outResults->AddInt32("unverified", unverifiedCount);

	return B_OK;
}

void
ScanThread::_SendProgress(int current, int total)
{
	if (!fWindow)
		return;
	
	BMessage progress(MSG_SCAN_PROGRESS);
	progress.AddInt32("current", current);
	progress.AddInt32("total", total);
	
	BMessenger messenger(fWindow);
	messenger.SendMessage(&progress);
}

void
ScanThread::_SendComplete(BMessage* results)
{
	if (!fWindow || !results)
		return;
	
	BMessage complete(MSG_SCAN_COMPLETE);
	complete.AddMessage("results", results);
	
	BMessenger messenger(fWindow);
	messenger.SendMessage(&complete);
}
