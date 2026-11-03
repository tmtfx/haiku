#ifndef _INTEGRITY_CHECKER_APP_H
#define _INTEGRITY_CHECKER_APP_H

#include <Application.h>
#include <String.h>

class IntegrityCheckerWindow;

class IntegrityCheckerApp : public BApplication {
public:
	IntegrityCheckerApp();
	virtual ~IntegrityCheckerApp();
	
	virtual void ReadyToRun();
	virtual void MessageReceived(BMessage* message);
	virtual bool QuitRequested();
	
private:
	IntegrityCheckerWindow* fWindow;
	
	void _CreateWindow();
	void _ShowWindow();
	void _LoadPreferences();
	void _SavePreferences();
};

#endif // _INTEGRITY_CHECKER_APP_H
