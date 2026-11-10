#include <Application.h>
#include <stdio.h>

#include "ControlsWindow.h"
#include "MainWindow.h"


class BttvViewerApp : public BApplication {
public:
	BttvViewerApp()
		:
		BApplication("application/x-vnd.Haiku-BttvViewer"),
		fControlsWindow(NULL)
	{
	}

	virtual void ReadyToRun()
	{
		MainWindow* window = new MainWindow();
		// Grabbed once, right away, independently of MainWindow's own
		// lifetime: QuitRequested() below must not assume MainWindow (or
		// its fControlsWindow member) is still around by the time it runs.
		fControlsWindow = window->GetControlsWindow();
		window->Show();
	}

	// MainWindow used to Lock()+Quit() fControlsWindow itself, from its
	// own ~MainWindow() -- i.e. from MainWindow's thread, while that
	// thread was itself mid-teardown of MainWindow. That hung
	// indefinitely every time (confirmed via logging: execution stopped
	// exactly at fControlsWindow->Lock(), never returned). Doing it here
	// instead runs it on the *application* thread, which holds none of
	// MainWindow's locks, breaking the cross-window lock-ordering cycle.
	virtual bool QuitRequested()
	{
		fprintf(stderr, "[VIEWER] BttvViewerApp::QuitRequested: ENTER\n");
		if (fControlsWindow != NULL) {
			fprintf(stderr, "[VIEWER] BttvViewerApp::QuitRequested: "
				"locking ControlsWindow...\n");
			if (fControlsWindow->Lock()) {
				fprintf(stderr, "[VIEWER] BttvViewerApp::QuitRequested: "
					"locked, calling Quit()...\n");
				// No Unlock() after Quit(): Quit() destroys the window
				// (ControlsWindow::QuitRequested() isn't consulted by a
				// direct Quit() call, so its "just Hide()" override
				// doesn't veto this), so both the lock and the object it
				// guards are gone once Quit() returns -- Unlock() here
				// would be a use-after-free.
				fControlsWindow->Quit();
			} else {
				fprintf(stderr, "[VIEWER] BttvViewerApp::QuitRequested: "
					"Lock() FAILED\n");
			}
			fControlsWindow = NULL;
		}
		fprintf(stderr, "[VIEWER] BttvViewerApp::QuitRequested: EXIT\n");
		return true;
	}

private:
	ControlsWindow*	fControlsWindow;
};


int
main(int argc, char* argv[])
{
	(void)argc;
	(void)argv;

	BttvViewerApp app;
	app.Run();
	return 0;
}
