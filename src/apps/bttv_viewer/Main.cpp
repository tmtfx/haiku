#include <Application.h>

#include "MainWindow.h"


class BttvViewerApp : public BApplication {
public:
	BttvViewerApp()
		:
		BApplication("application/x-vnd.Haiku-BttvViewer")
	{
	}

	virtual void ReadyToRun()
	{
		MainWindow* window = new MainWindow();
		window->Show();
	}
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
