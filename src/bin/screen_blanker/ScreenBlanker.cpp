/*
 * Copyright 2003-2014 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Jérôme Duval, jerome.duval@free.fr
 *		Axel Dörfler, axeld@pinc-software.de
 *		Ryan Leavengood, leavengood@gmail.com
 *		Michael Phipps
 *		John Scipione, jscipione@gmail.com
 *		Puck Meerburg, puck@puckipedia.nl
 */


#include "ScreenBlanker.h"

#include <Beep.h>
#include <Debug.h>
#include <File.h>
#include <FindDirectory.h>
#include <Messenger.h>
#include <Path.h>
#include <Roster.h>
#include <Screen.h>
#include <StorageDefs.h>
#include <SupportDefs.h>
#include <image.h>

#include <stdio.h>
#include <string.h>
#include <syslog.h>

#include "ScreenSaverShared.h"
#include "../../servers/fingerprint/defs.h"

const static uint32 kMsgTurnOffScreen = 'tofs';
const static uint32 kMsgSuspendScreen = 'suss';
const static uint32 kMsgStandByScreen = 'stbs';


//	#pragma mark - ScreenBlanker


ScreenBlanker::ScreenBlanker()
	:
	BApplication(SCREEN_BLANKER_SIG),
	fWindow(NULL),
	fSaverRunner(NULL),
	fPasswordWindow(NULL),
	fImmediateLock(false),
	fTestSaver(false),
	fResumeRunner(NULL),
	fStandByScreenRunner(NULL),
	fSuspendScreenRunner(NULL),
	fTurnOffScreenRunner(NULL),
	fFingerprintConfigured(false),
	fFingerprintBusy(false)
{
	fBlankTime = system_time();
}


ScreenBlanker::~ScreenBlanker()
{
	delete fResumeRunner;
	_TurnOnScreen();
}


void
ScreenBlanker::ReadyToRun()
{
	if (!fSettings.Load())
		fprintf(stderr, "could not load settings, using defaults\n");

	// create a BDirectWindow and start the render thread.
	// TODO: we need a window per screen...
	BScreen screen(B_MAIN_SCREEN_ID);
	fWindow = new ScreenSaverWindow(screen.Frame(), fTestSaver);
	fPasswordWindow = new PasswordWindow();
	_CheckFingerprintConfigured();
	_UpdateFingerprintButton();

	BView* view = fWindow->ChildAt(0);
	fSaverRunner = new ScreenSaverRunner(fWindow, view, fSettings);
	fWindow->SetSaverRunner(fSaverRunner);

	BScreenSaver* saver = fSaverRunner->ScreenSaver();
	if (saver != NULL && saver->StartSaver(view, false) == B_OK)
		fSaverRunner->Run();
	else {
		fprintf(stderr, "could not load the screensaver addon\n");
		view->SetViewColor(0, 0, 0);
			// needed for Blackness saver
	}

	fWindow->SetFullScreen(true);
	fWindow->Show();
	HideCursor();

	_QueueTurnOffScreen();
}


void
ScreenBlanker::_TurnOnScreen()
{
	delete fStandByScreenRunner;
	delete fSuspendScreenRunner;
	delete fTurnOffScreenRunner;

	fStandByScreenRunner = fSuspendScreenRunner = fTurnOffScreenRunner = NULL;

	BScreen screen;
	screen.SetDPMS(B_DPMS_ON);
}


void
ScreenBlanker::_SetDPMSMode(uint32 mode)
{
	// called with B_DPMS_OFF, B_DPMS_SUSPEND or B_DPSM_STAND_BY
	// depending on screen DPMS capabilities
	BScreen screen;
	if (screen.SetDPMS(mode) != B_OK) {
		fprintf(stderr, "Failed to set DPMS mode %u: keep running screen saver\n", mode);
		return;
	}

	if (fWindow->Lock()) {
		fSaverRunner->Suspend();
		fWindow->Unlock();
	}
}


void
ScreenBlanker::_ShowPasswordWindow()
{
	_TurnOnScreen();

	if (fWindow->Lock()) {
		fSaverRunner->Suspend();

		fWindow->Sync();
			// TODO: is that needed?
		ShowCursor();
		if (fPasswordWindow->IsHidden())
			fPasswordWindow->Show();

		fWindow->Unlock();
	}

	_QueueResumeScreenSaver();
}


void
ScreenBlanker::_QueueResumeScreenSaver()
{
	delete fResumeRunner;

	BMessage resume(kMsgResumeSaver);
	fResumeRunner = new BMessageRunner(BMessenger(this), &resume,
		fSettings.BlankTime(), 1);
	if (fResumeRunner->InitCheck() != B_OK)
		syslog(LOG_ERR, "resume screen saver runner failed\n");
}


void
ScreenBlanker::_QueueTurnOffScreen()
{
	// stop running notifiers

	delete fStandByScreenRunner;
	delete fSuspendScreenRunner;
	delete fTurnOffScreenRunner;

	fStandByScreenRunner = fSuspendScreenRunner = fTurnOffScreenRunner = NULL;

	// figure out which notifiers we need and which of them are supported

	uint32 flags = fSettings.TimeFlags();
	BScreen screen;
	uint32 capabilities = screen.DPMSCapabilites();
	if ((capabilities & B_DPMS_OFF) == 0)
		flags &= ~ENABLE_DPMS_OFF;
	if ((capabilities & B_DPMS_SUSPEND) == 0)
		flags &= ~ENABLE_DPMS_SUSPEND;
	if ((capabilities & B_DPMS_STAND_BY) == 0)
		flags &= ~ENABLE_DPMS_STAND_BY;

	if ((flags & ENABLE_DPMS_MASK) == 0)
		return;

	if (fSettings.OffTime() == fSettings.SuspendTime()
		&& (flags & (ENABLE_DPMS_OFF | ENABLE_DPMS_SUSPEND))
			== (ENABLE_DPMS_OFF | ENABLE_DPMS_SUSPEND))
		flags &= ~ENABLE_DPMS_SUSPEND;
	if (fSettings.SuspendTime() == fSettings.StandByTime()
		&& (flags & (ENABLE_DPMS_SUSPEND | ENABLE_DPMS_STAND_BY))
			== (ENABLE_DPMS_SUSPEND | ENABLE_DPMS_STAND_BY))
		flags &= ~ENABLE_DPMS_STAND_BY;

	// start them off again

	if (flags & ENABLE_DPMS_STAND_BY) {
		BMessage dpms(kMsgStandByScreen);
		fStandByScreenRunner = new BMessageRunner(BMessenger(this), &dpms,
			fSettings.StandByTime(), 1);
		if (fStandByScreenRunner->InitCheck() != B_OK)
			syslog(LOG_ERR, "standby screen saver runner failed\n");
	}

	if (flags & ENABLE_DPMS_SUSPEND) {
		BMessage dpms(kMsgSuspendScreen);
		fSuspendScreenRunner = new BMessageRunner(BMessenger(this), &dpms,
			fSettings.SuspendTime(), 1);
		if (fSuspendScreenRunner->InitCheck() != B_OK)
			syslog(LOG_ERR, "suspend screen saver runner failed\n");
	}

	if (flags & ENABLE_DPMS_OFF) {
		BMessage dpms(kMsgTurnOffScreen);
		fTurnOffScreenRunner = new BMessageRunner(BMessenger(this), &dpms,
			fSettings.OffTime(), 1);
		if (fTurnOffScreenRunner->InitCheck() != B_OK)
			syslog(LOG_ERR, "turn off screen saver runner failed\n");
	}
}


void
ScreenBlanker::ArgvReceived(int argc, char** argv)
{
	if (argc > 1 && strcmp("-l", argv[1]) == 0)
		fImmediateLock = true;
}


void
ScreenBlanker::_CheckFingerprintConfigured()
{
	fFingerprintConfigured = false;

	if (!be_roster->IsRunning(FINGERPRINT_SERVER_SIGNATURE))
		return;

	BMessenger server(FINGERPRINT_SERVER_SIGNATURE);
	if (!server.IsValid())
		return;

	BMessage enabledRequest(B_FP_GET_SCREENSAVER_UNLOCK), enabledReply;
	if (server.SendMessage(&enabledRequest, &enabledReply) != B_OK)
		return;
	bool enabled = false;
	enabledReply.FindBool("enabled", &enabled);
	if (!enabled)
		return;

	BMessage listRequest(B_FP_LIST), listReply;
	if (server.SendMessage(&listRequest, &listReply) != B_OK)
		return;

	fFingerprintConfigured = listReply.HasInt32("fingerId");
}


void
ScreenBlanker::_UpdateFingerprintButton()
{
	fPasswordWindow->SetFingerprintAvailable(
		fFingerprintConfigured && !fFingerprintBusy);
}


void
ScreenBlanker::_StartFingerprintVerify()
{
	if (fFingerprintBusy)
		return;

	fFingerprintBusy = true;
	_UpdateFingerprintButton();

	thread_id thread = spawn_thread(_FingerprintThreadEntry,
		"screen_blanker_fp_verify", B_NORMAL_PRIORITY, this);
	resume_thread(thread);
}


status_t
ScreenBlanker::_FingerprintThreadEntry(void* data)
{
	ScreenBlanker* app = (ScreenBlanker*)data;

	BMessenger server(FINGERPRINT_SERVER_SIGNATURE);
	BMessage request(B_FP_VERIFY_START), reply;
	status_t status = server.SendMessage(&request, &reply);

	BMessage result(kMsgFingerprintResult);
	int32 error = B_ERROR;
	if (status == B_OK)
		reply.FindInt32("error", &error);
	else
		error = status;
	result.AddInt32("error", error);

	app->PostMessage(&result);
	return B_OK;
}


void
ScreenBlanker::_CancelFingerprintVerify()
{
	if (!fFingerprintBusy)
		return;

	// Best-effort only: a VERIFY_START already blocked inside the sensor's
	// wait-for-finger call cannot actually be interrupted this way (see
	// B_FP_VERIFY_CANCEL's documentation in defs.h) - this just covers the
	// narrow window before that blocking wait begins. Either way, the
	// worker thread started by _StartFingerprintVerify() will eventually
	// finish on its own (finger placed, reader unplugged, or
	// fingerprint_server restarted) and its kMsgFingerprintResult will
	// simply be ignored if we're not showing the password window anymore.
	BMessenger server(FINGERPRINT_SERVER_SIGNATURE);
	BMessage cancel(B_FP_VERIFY_CANCEL);
	server.SendMessage(&cancel);
}


void
ScreenBlanker::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgUnlock:
		{
			// if the user has no password then just exit on any input
			if (fSettings.Password()[0] != '\0'
				&& strcmp(fSettings.Password(), crypt(fPasswordWindow->Password(),
					fSettings.Password())) != 0) {
				beep();
				fPasswordWindow->SetPassword("");
				_QueueResumeScreenSaver();
			} else  {
				PRINT(("Quitting!\n"));
				_CancelFingerprintVerify();
				_Shutdown();
				Quit();
			}
			break;
		}

		case kMsgFingerprintStart:
		{
			_StartFingerprintVerify();
			break;
		}

		case kMsgFingerprintResult:
		{
			fFingerprintBusy = false;

			int32 error = B_ERROR;
			message->FindInt32("error", &error);
			if (error == B_OK) {
				PRINT(("Fingerprint match, quitting!\n"));
				_Shutdown();
				Quit();
			} else {
				beep();
				_UpdateFingerprintButton();
			}
			break;
		}

		case kMsgResumeSaver:
		{
			_CancelFingerprintVerify();

			if (fWindow->Lock()) {
				// ensure that our window and application are active before calling HideCursor()
				fWindow->Activate();
				HideCursor();
				fPasswordWindow->SetPassword("");
				fPasswordWindow->Hide();

				fSaverRunner->Resume();
				fWindow->Unlock();
			}

			// Turn on the message filter again
			BMessage enable(kMsgEnableFilter);
			fWindow->PostMessage(&enable);

			_QueueTurnOffScreen();
			break;
		}

		case kMsgTurnOffScreen:
			_SetDPMSMode(B_DPMS_OFF);
			break;

		case kMsgSuspendScreen:
			_SetDPMSMode(B_DPMS_SUSPEND);
			break;

		case kMsgStandByScreen:
			_SetDPMSMode(B_DPMS_STAND_BY);
			break;

		case kMsgTestSaver:
			fTestSaver = true;
			break;

		default:
			BApplication::MessageReceived(message);
	}
}


bool
ScreenBlanker::QuitRequested()
{
	if (fSettings.LockEnable()) {
		bigtime_t minTime = fSettings.PasswordTime() - fSettings.BlankTime();
		if (minTime == 0)
			minTime = 5000000;
		if (fImmediateLock || system_time() - fBlankTime > minTime) {
			_ShowPasswordWindow();
			return false;
		}
	}

	_Shutdown();
	return true;
}


bool
ScreenBlanker::IsPasswordWindowShown() const
{
	return fPasswordWindow != NULL && !fPasswordWindow->IsHidden();
}


void
ScreenBlanker::_Shutdown()
{
	if (fWindow != NULL) {
		fWindow->Hide();

		if (fWindow->Lock())
			fWindow->Quit();
	}

	delete fSaverRunner;
	fSaverRunner = NULL;
}


//	#pragma mark - main


int
main(int argc, char** argv)
{
	ScreenBlanker app;
	app.Run();

	return 0;
}
