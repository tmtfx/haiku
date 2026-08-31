/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include "MainWindow.h"
#include <TextControl.h>
#include <Button.h>
#include <ScrollView.h>
#include <LayoutBuilder.h>
#include <SplitView.h>
#include <Application.h>
#include <String.h>
#include <stdio.h>

static const uint32 MSG_SEND_CLICKED = 'SEND';
static const uint32 MSG_ABRT_CLICKED = 'ABCL';

void InputTextView::SetEnabled(bool enable) {
	fEnabled = enable;
	if (enable) {
		SetViewColor(ui_color(B_DOCUMENT_BACKGROUND_COLOR));
		Invalidate();
	} else {
		 SetViewColor(ui_color(B_TOOL_TIP_BACKGROUND_COLOR));
		 Invalidate();
	}
}
bool InputTextView::IsEnabled() {
	return fEnabled;
}

void InputTextView::KeyDown(const char* bytes,
					 int32 numBytes) {
	if (numBytes == 1){ //single char
		if ((bytes[0] == B_ENTER) && !(modifiers() & (B_SHIFT_KEY| B_OPTION_KEY | B_CONTROL_KEY))) {
			if (fEnabled) {
				be_app->WindowAt(0)->PostMessage(MSG_SEND_CLICKED);
				return;
			} else {
				return;
			}
		}
	}
	return BTextView::KeyDown(bytes,numBytes);
}

MainWindow::MainWindow(const char* context)
	: BWindow(BRect(100, 100, 600, 500), "Haiku MCP Client", B_TITLED_WINDOW, B_AUTO_UPDATE_SIZE_LIMITS)
{
	if (context != nullptr && context[0] != '\0') {
		fEngine = new AIEngine(context);
	} else {
		fEngine = new AIEngine();
	}

	fHistoryView = new BTextView("history");
	fHistoryView->MakeEditable(false);
	
	if (context != nullptr && context[0] != '\0') {
		BString title;
		if (fEngine->GetTitle(title) == B_OK && !title.IsEmpty()) {
			SetTitle(title.String());
			BString msg;
			msg.SetToFormat("[Contesto: %s]\n\n", title.String());
			_AppendText(msg.String());
		} else {
			BString msg;
			msg.SetToFormat("[Ripristinato contesto: %s]\n\n", context);
			_AppendText(msg.String());
		}
	}
	fHistoryView->MakeEditable(false);
	
	fHistoryScroll = new BScrollView("history_scroll", fHistoryView, B_WILL_DRAW, false, true);
	fInputView = new InputTextView("input");
	fInputScroll = new BScrollView("input_scroll", fInputView, B_WILL_DRAW, false, true);
	fInputView->SetEnabled(true);
	fSendButton = new BButton("send", "Invia", new BMessage(MSG_SEND_CLICKED));
	fAbortButton = new BButton("send", "Interrompi", new BMessage(MSG_ABRT_CLICKED));

	fSendButton->SetExplicitMaxSize(BSize(100, B_SIZE_UNLIMITED));
	fAbortButton->SetExplicitMaxSize(BSize(100, B_SIZE_UNLIMITED));
	fAbortButton->SetEnabled(false);
	fInputScroll->SetExplicitMinSize(BSize(B_SIZE_UNSET, 80));   // Altezza minima per digitare
	fInputScroll->SetExplicitMaxSize(BSize(B_SIZE_UNSET, B_SIZE_UNLIMITED)); // Può espandersi a piacere
	fHistoryScroll->SetExplicitMinSize(BSize(B_SIZE_UNSET, 100));
	fHistoryScroll->SetExplicitMaxSize(BSize(B_SIZE_UNSET, B_SIZE_UNLIMITED));

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.SetInsets(10)
		.AddSplit(B_VERTICAL, B_USE_SMALL_SPACING)
			.Add(fHistoryScroll, 7.0f)
			.AddGroup(B_HORIZONTAL, 10, 3.0f)
				.Add(fInputScroll, 1.0f)
				.AddGroup(B_VERTICAL, 5, 0.0f) // 0.0f evita che i pulsanti forzino l'altezza
					.Add(fAbortButton)
					.Add(fSendButton)
				.End()
			.End()
		.End()
	.End();

	// Sposta il focus sul box di testo per poter digitare subito
	fInputView->MakeFocus(true);
}

MainWindow::~MainWindow()
{
	//delete fEngine;
}

void MainWindow::MessageReceived(BMessage* msg)
{
	switch (msg->what) {
		case MSG_SEND_CLICKED:
			_OnSend();
			break;
		case MSG_ABRT_CLICKED:
			fEngine->Abort();
			break;
		case MSG_AI_RESPONSE: {
			bool complete = false;
			msg->FindBool("complete", &complete);
			
			if (!complete) {
				// Pezzettino di stream asincrono arrivato
				BString partialToken = msg->FindString("partial");
				if (partialToken.Length() > 0) {
					_AppendText(partialToken.String());
					// Scorri in automatico verso il basso per seguire lo stream
					fHistoryView->ScrollToSelection();
				}
			} else {
				// Generazione completata con successo
				BString response = msg->FindString("response");
				status_t status = B_OK;
				msg->FindInt32("status", &status);
				
				if (status != B_OK) {
					_AppendText("\n[Errore di generazione]\n");
				} else {
					_AppendText("\n\n");
				}
				
				fHistoryView->ScrollToSelection();
				fInputView->SetEnabled(true);
				fSendButton->SetEnabled(true);
				fInputView->MakeFocus(true);
				fAbortButton->SetEnabled(false);
			}
			break;
		}
		case MSG_AI_ERROR: {
			_AppendText("\n[Errore del Server o del Plugin]\n");
			fHistoryView->ScrollToSelection();
			fInputView->SetEnabled(true);
			fSendButton->SetEnabled(true);
			fInputView->MakeFocus(true);
			break;
		}
		case MSG_AI_TITLE_CHANGED: {
			const char* title_append=nullptr;
			if (msg->FindString("title",&title_append) == B_OK && title_append != nullptr) {
				BString title("Haiku MCP Client");
				title.Append(" - ");
				title.Append(title_append);
				SetTitle(title.String());
			}
			break;
		}
		default:
			BWindow::MessageReceived(msg);
			break;
	}
}

bool MainWindow::QuitRequested()
{
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}

void MainWindow::_OnSend()
{
	BString text = fInputView->Text();
	text.Trim();
	if (text.IsEmpty()) return;

	fInputView->SetEnabled(false);
	fSendButton->SetEnabled(false);

	_AppendText("Tu: ");
	_AppendText(text.String());
	_AppendText("\n\nLLM: ");
	fHistoryView->ScrollToSelection();

	fInputView->SetText("");

	status_t err = fEngine->GenerateAsync(text.String(), BMessenger(this));
	if (err != B_OK) {
		_AppendText("[Errore di connessione al server]\n\n");
		fHistoryView->ScrollToSelection();
		fInputView->SetEnabled(true);
		fSendButton->SetEnabled(true);
		fInputView->MakeFocus(true);
		return;
	}
	fAbortButton->SetEnabled(true);
}

void MainWindow::_AppendText(const char* text)
{
	if (text == nullptr || text[0] == '\0')
		return;
	
	int32 len = fHistoryView->TextLength();
	fHistoryView->Select(len, len);
	fHistoryView->Insert(text);
}
