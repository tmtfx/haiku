/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include <Application.h>

#include "FingerprintWindow.h"


class FingerprintApp : public BApplication {
public:
	FingerprintApp()
		:
		BApplication("application/x-vnd.Haiku-Fingerprint")
	{
	}

	virtual void ReadyToRun()
	{
		(new FingerprintWindow())->Show();
	}
};


int
main()
{
	FingerprintApp app;
	app.Run();
	return 0;
}
