#include "IntegrityCheckerApp.h"
#include "IntegrityCheckerWindow.h"

#include <Application.h>
#include <File.h>
#include <FindDirectory.h>
#include <Path.h>
#include <stdio.h>

// Message codes
#define MSG_WINDOW_CLOSED 'WnCl'
#define MSG_QUIT_APP 'QuAp'

IntegrityCheckerApp::IntegrityCheckerApp()
	: BApplication("application/x-vnd.pirati-del-frico-integrity-checker"),
	  fWindow(NULL)
{
}

IntegrityCheckerApp::~IntegrityCheckerApp()
{
}

void
IntegrityCheckerApp::ReadyToRun()
{
	_LoadPreferences();
	_CreateWindow();
	_ShowWindow();
}

void
IntegrityCheckerApp::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case MSG_WINDOW_CLOSED:
			fWindow = NULL;
			PostMessage(MSG_QUIT_APP);
			break;
		
		case MSG_QUIT_APP:
			_SavePreferences();
			Quit();
			break;
		
		default:
			BApplication::MessageReceived(message);
			break;
	}
}

bool
IntegrityCheckerApp::QuitRequested()
{
	if (fWindow) {
		fWindow->PostMessage(B_QUIT_REQUESTED);
	}
	return true;
}

void
IntegrityCheckerApp::_CreateWindow()
{
	if (fWindow)
		return;
	
	// Create window (will be deleted by application when closed)
	fWindow = new IntegrityCheckerWindow();
	if (!fWindow)
		return;
	
	// Window is created but not shown yet
	fWindow->Show();
}

void
IntegrityCheckerApp::_ShowWindow()
{
	if (fWindow) {
		fWindow->Activate();
	}
}

void
IntegrityCheckerApp::_LoadPreferences()
{
	// TODO: Load window position, size, and app preferences
	// Preferences path: ~/config/settings/pirati.del.frico/integrity_checker.config
}

void
IntegrityCheckerApp::_SavePreferences()
{
	// TODO: Save window position, size, and app preferences
}

// ============================================================================
// Main Entry Point
// ============================================================================

int
main(int argc, char* argv[])
{
	IntegrityCheckerApp app;
	app.Run();
	return 0;
}
