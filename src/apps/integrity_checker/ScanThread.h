#ifndef _SCAN_THREAD_H
#define _SCAN_THREAD_H

#include <Message.h>
#include <Looper.h>
#include <OS.h>

class IntegrityCheckerWindow;

// Haiku's public API has no BThread class (that's a BeOS-era misconception);
// background work is done with the kernel primitives directly.
class ScanThread {
public:
	ScanThread(IntegrityCheckerWindow* window);
	~ScanThread();

	status_t Run();
	bool IsRunning() const;

	void SetWindow(IntegrityCheckerWindow* window) { fWindow = window; }
	void RequestQuit() { fQuitRequested = true; }

private:
	static status_t _ThreadEntry(void* data);
	status_t _Execute();
	status_t _RunScan(BMessage* outResults);
	status_t _ExecuteFsckHpkgs(BMessage* outResults);
	void _SendProgress(int current, int total);
	void _SendComplete(BMessage* results);

	IntegrityCheckerWindow* fWindow;
	thread_id fThread;
	volatile bool fQuitRequested;
};

#endif // _SCAN_THREAD_H
