#include "ScanThread.h"
#include "IntegrityCheckerWindow.h"

#include <Message.h>
#include <Messenger.h>
#include <OS.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

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
	// TODO: Execute fsck_hpkgs scan command and capture output
	// For now, simulate a scan
	
	const char* packages[] = {
		"haiku-1.0.hpkg",
		"curl-7.85.0-2.hpkg",
		"openssl-1.1.1-8.hpkg",
		"zlib-1.2.11-3.hpkg",
		"gcc-11.2.0-1.hpkg"
	};
	
	int total = sizeof(packages) / sizeof(packages[0]);
	
	for (int i = 0; i < total; i++) {
		if (fQuitRequested)
			return B_INTERRUPTED;
		
		_SendProgress(i + 1, total);
		
		// Simulate processing time
		snooze(100000);  // 100ms per package
		
		// Add package result to message
		BMessage packageInfo;
		packageInfo.AddString("name", packages[i]);
		
		// Simulate status: mostly ok, one corrupted, one unverified
		if (i == 1) {
			packageInfo.AddString("status", "corrupted");
			packageInfo.AddString("checksum_expected", "abc123def456...");
			packageInfo.AddString("checksum_actual", "xyz789uvw012...");
			packageInfo.AddString("source", "repository");
		} else if (i == 3) {
			packageInfo.AddString("status", "unverified");
			packageInfo.AddString("reason", "Missing attribute");
		} else {
			packageInfo.AddString("status", "ok");
			packageInfo.AddString("checksum", "abc123def456...");
			packageInfo.AddString("verified_time", "2026-11-03T06:30:00 UTC");
		}
		
		outResults->AddMessage("package", &packageInfo);
	}
	
	outResults->AddInt32("total", total);
	outResults->AddInt32("ok", total - 2);
	outResults->AddInt32("corrupted", 1);
	outResults->AddInt32("unverified", 1);
	
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
