/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include <Application.h>

#include "FingerprintPasswordGateWindow.h"
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
		// Gate behind the master password: without this, anyone with
		// access to the running session could freely enroll/delete
		// fingerprint templates and set up the sequence-escrow feature
		// (Fase 1e) with no authentication at all - see
		// FingerprintPasswordGateWindow.h for the full rationale.
		if (FingerprintPasswordGateWindow::CheckAccess() != B_OK) {
			PostMessage(B_QUIT_REQUESTED);
			return;
		}

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
