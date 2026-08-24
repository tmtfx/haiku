/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include <Window.h>
#include <AICommands.h>
#include <TextView.h>

//class BTextView;
class BTextControl;
class BButton;
class BScrollView;

class InputTextView : public BTextView {
public:
	InputTextView(const char* name) : BTextView(name) 
	{
		fEnabled=true;
	};
	//virtual ~InputTextView() {};
	virtual void KeyDown(const char* bytes, int32 numBytes);
	void SetEnabled(bool enable);
	bool IsEnabled();
private:
	bool fEnabled;
};

class MainWindow : public BWindow {
public:
    MainWindow(const char* context = nullptr);
    virtual ~MainWindow();

    virtual void MessageReceived(BMessage* msg);
    virtual bool QuitRequested();

private:
    void _OnSend();
    void _AppendText(const char* text);

    BTextView*    fHistoryView;
    BScrollView*  fHistoryScroll;
    InputTextView*    fInputView;
    BScrollView*  fInputScroll;
    //BTextControl* fInputControl;
    BButton*      fSendButton;
    BButton*      fAbortButton;

    AIEngine*     fEngine;
};

#endif // MAIN_WINDOW_H
