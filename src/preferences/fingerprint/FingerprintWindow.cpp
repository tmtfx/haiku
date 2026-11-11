/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include "FingerprintWindow.h"

#include <stdio.h>
#include <string.h>

#include <Alert.h>
#include <Application.h>
#include <Button.h>
#include <CheckBox.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <Message.h>
#include <Messenger.h>
#include <OS.h>
#include <Roster.h>
#include <ScrollView.h>
#include <StringView.h>

#include "FingerListItem.h"
#include "../../servers/fingerprint/defs.h"
#include "../../../headers/private/app/KeyStoreDefs.h"


enum {
	kMsgEnroll = 'fpEn',
	kMsgDelete = 'fpDl',
	kMsgVerify = 'fpVf',
	kMsgEnrollResult = 'fpER',
	kMsgVerifyResult = 'fpVR',
	kMsgToggleMasterPassword = 'fpMp',
};


using namespace BPrivate;


static BMessenger
ServerMessenger()
{
	if (!be_roster->IsRunning(FINGERPRINT_SERVER_SIGNATURE))
		be_roster->Launch(FINGERPRINT_SERVER_SIGNATURE);
	return BMessenger(FINGERPRINT_SERVER_SIGNATURE);
}


FingerprintWindow::FingerprintWindow()
	:
	BWindow(BRect(100, 100, 480, 420), "Fingerprint",
		B_TITLED_WINDOW, B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS)
{
	fListView = new BListView("fingerList");
	BScrollView* scrollView = new BScrollView("scroll", fListView,
		0, false, true);

	fEnrollButton = new BButton("enroll", "Registra nuova impronta",
		new BMessage(kMsgEnroll));
	fDeleteButton = new BButton("delete", "Elimina selezionata",
		new BMessage(kMsgDelete));
	fVerifyButton = new BButton("verify", "Test verifica",
		new BMessage(kMsgVerify));

	fMasterPasswordCheckBox = new BCheckBox("useForMasterPassword",
		"Usa l'impronta selezionata per sbloccare la master password",
		new BMessage(kMsgToggleMasterPassword));

	fStatusView = new BStringView("status", "Pronto.");

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(scrollView)
		.AddGroup(B_HORIZONTAL)
			.Add(fEnrollButton)
			.Add(fDeleteButton)
			.Add(fVerifyButton)
		.End()
		.Add(fMasterPasswordCheckBox)
		.Add(fStatusView)
	.End();

	_RefreshList();
}


FingerprintWindow::~FingerprintWindow()
{
}


bool
FingerprintWindow::QuitRequested()
{
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}


void
FingerprintWindow::_SetBusy(bool busy, const char* status)
{
	fEnrollButton->SetEnabled(!busy);
	fDeleteButton->SetEnabled(!busy);
	fVerifyButton->SetEnabled(!busy);
	fStatusView->SetText(status);
}


void
FingerprintWindow::_RefreshList()
{
	while (fListView->CountItems() > 0)
		delete fListView->RemoveItem((int32)0);

	BMessage request(B_FP_LIST);
	BMessage reply;
	status_t status = ServerMessenger().SendMessage(&request, &reply);
	if (status != B_OK) {
		fStatusView->SetText("Impossibile contattare fingerprint_server.");
		return;
	}

	int32 fingerId;
	BString label;
	int64 createdAt;
	for (int32 i = 0; reply.FindInt32("fingerId", i, &fingerId) == B_OK; i++) {
		reply.FindString("label", i, &label);
		reply.FindInt64("createdAt", i, &createdAt);
		fListView->AddItem(new FingerListItem(fingerId, label.String(),
			createdAt));
	}

	if (fListView->CountItems() == 0)
		fStatusView->SetText("Nessuna impronta registrata.");
	else
		fStatusView->SetText("Pronto.");
}


void
FingerprintWindow::_StartEnroll()
{
	_SetBusy(true, "Registrazione: appoggia e solleva il dito 3 volte "
		"quando richiesto dal sensore...");

	BMessage* request = new BMessage(B_FP_ENROLL_START);
	request->AddString("label", "Dito");

	struct Args {
		FingerprintWindow* window;
		BMessage* request;
	};
	Args* args = new Args { this, request };

	thread_id thread = spawn_thread(_EnrollThreadEntry, "fp_preflet_enroll",
		B_NORMAL_PRIORITY, args);
	resume_thread(thread);
}


status_t
FingerprintWindow::_EnrollThreadEntry(void* data)
{
	struct Args {
		FingerprintWindow* window;
		BMessage* request;
	};
	Args* args = (Args*)data;

	BMessage reply;
	status_t status = ServerMessenger().SendMessage(args->request, &reply);
	delete args->request;

	if (status != B_OK) {
		reply.MakeEmpty();
		reply.AddInt32("fingerId", -1);
		reply.AddInt32("error", status);
	}
	reply.what = kMsgEnrollResult;
	args->window->PostMessage(&reply);

	delete args;
	return B_OK;
}


void
FingerprintWindow::_StartDelete()
{
	int32 selected = fListView->CurrentSelection();
	if (selected < 0) {
		fStatusView->SetText("Seleziona prima un'impronta dalla lista.");
		return;
	}
	FingerListItem* item = (FingerListItem*)fListView->ItemAt(selected);

	BMessage request(B_FP_DELETE);
	request.AddInt32("fingerId", item->FingerId());
	BMessage reply;
	status_t status = ServerMessenger().SendMessage(&request, &reply);

	int32 error = B_ERROR;
	reply.FindInt32("error", &error);
	if (status == B_OK && error == B_OK) {
		fStatusView->SetText("Impronta eliminata.");
		_RefreshList();
	} else {
		fStatusView->SetText("Errore durante l'eliminazione.");
	}
}


void
FingerprintWindow::_StartVerify()
{
	_SetBusy(true, "Verifica: appoggia il dito sul sensore...");

	BMessage* request = new BMessage(B_FP_VERIFY_START);

	struct Args {
		FingerprintWindow* window;
		BMessage* request;
	};
	Args* args = new Args { this, request };

	thread_id thread = spawn_thread(_VerifyThreadEntry, "fp_preflet_verify",
		B_NORMAL_PRIORITY, args);
	resume_thread(thread);
}


status_t
FingerprintWindow::_VerifyThreadEntry(void* data)
{
	struct Args {
		FingerprintWindow* window;
		BMessage* request;
	};
	Args* args = (Args*)data;

	BMessage reply;
	status_t status = ServerMessenger().SendMessage(args->request, &reply);
	delete args->request;

	if (status != B_OK) {
		reply.MakeEmpty();
		reply.AddInt32("fingerId", -1);
		reply.AddInt32("score", 0);
		reply.AddInt32("error", status);
	}
	reply.what = kMsgVerifyResult;
	args->window->PostMessage(&reply);

	delete args;
	return B_OK;
}


void
FingerprintWindow::_ToggleMasterPasswordUnlock(bool enable)
{
	BMessenger keyStore(kKeyStoreServerSignature);
	if (!keyStore.IsValid()) {
		fStatusView->SetText("keystore_server non raggiungibile.");
		fMasterPasswordCheckBox->SetValue(enable ? B_CONTROL_OFF
			: B_CONTROL_ON);
		return;
	}

	BMessage request;
	if (enable) {
		int32 selected = fListView->CurrentSelection();
		if (selected < 0) {
			fStatusView->SetText("Seleziona prima un'impronta dalla lista.");
			fMasterPasswordCheckBox->SetValue(B_CONTROL_OFF);
			return;
		}
		FingerListItem* item = (FingerListItem*)fListView->ItemAt(selected);
		request.what = KEY_STORE_ENABLE_FINGERPRINT_UNLOCK;
		request.AddInt32("fingerId", item->FingerId());
	} else {
		request.what = KEY_STORE_DISABLE_FINGERPRINT_UNLOCK;
	}

	BMessage reply;
	status_t status = keyStore.SendMessage(&request, &reply);
	if (status != B_OK || reply.what != KEY_STORE_SUCCESS) {
		int32 result = B_ERROR;
		reply.FindInt32("result", &result);
		char buf[160];
		snprintf(buf, sizeof(buf), "%s fallita: %s",
			enable ? "Attivazione" : "Disattivazione",
			strerror(status != B_OK ? status : result));
		fStatusView->SetText(buf);
		fMasterPasswordCheckBox->SetValue(enable ? B_CONTROL_OFF
			: B_CONTROL_ON);
		return;
	}

	fStatusView->SetText(enable
		? "Sblocco master password tramite impronta attivato."
		: "Sblocco master password tramite impronta disattivato.");
}


void
FingerprintWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgEnroll:
			_StartEnroll();
			break;

		case kMsgDelete:
			_StartDelete();
			break;

		case kMsgVerify:
			_StartVerify();
			break;

		case kMsgToggleMasterPassword:
			_ToggleMasterPasswordUnlock(
				fMasterPasswordCheckBox->Value() == B_CONTROL_ON);
			break;

		case kMsgEnrollResult:
		{
			int32 error = B_ERROR;
			int32 fingerId = -1;
			message->FindInt32("error", &error);
			message->FindInt32("fingerId", &fingerId);

			if (error == B_OK) {
				_SetBusy(false, "Impronta registrata.");
				_RefreshList();
			} else {
				char buf[128];
				snprintf(buf, sizeof(buf),
					"Registrazione fallita: %s", strerror(error));
				_SetBusy(false, buf);
			}
			break;
		}

		case kMsgVerifyResult:
		{
			int32 error = B_ERROR;
			int32 fingerId = -1;
			int32 score = 0;
			message->FindInt32("error", &error);
			message->FindInt32("fingerId", &fingerId);
			message->FindInt32("score", &score);

			char buf[128];
			if (error == B_OK) {
				snprintf(buf, sizeof(buf),
					"Match! Impronta #%ld (punteggio %ld)", (long)fingerId,
					(long)score);
			} else if (error == B_NAME_NOT_FOUND) {
				snprintf(buf, sizeof(buf), "Nessuna corrispondenza.");
			} else {
				snprintf(buf, sizeof(buf), "Verifica fallita: %s",
					strerror(error));
			}
			_SetBusy(false, buf);
			break;
		}

		default:
			BWindow::MessageReceived(message);
			break;
	}
}
