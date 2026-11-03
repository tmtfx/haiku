#ifndef _SCAN_THREAD_H
#define _SCAN_THREAD_H

#include <Thread.h>
#include <Message.h>
#include <Looper.h>

class IntegrityCheckerWindow;

class ScanThread : public BThread {
public:
	ScanThread(IntegrityCheckerWindow* window);
	virtual ~ScanThread();
	
	virtual status_t Execute();
	
	void SetWindow(IntegrityCheckerWindow* window) { fWindow = window; }
	void RequestQuit() { fQuitRequested = true; }
	
private:
	IntegrityCheckerWindow* fWindow;
	bool fQuitRequested;
	
	status_t _RunScan(BMessage* outResults);
	status_t _ExecuteFsckHpkgs(BMessage* outResults);
	void _SendProgress(int current, int total);
	void _SendComplete(BMessage* results);
};

#endif // _SCAN_THREAD_H
