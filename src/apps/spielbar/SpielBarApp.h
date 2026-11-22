/*
 * SpielBar v3 - BApplication e main()
 *
 * Generato da tools/split_app.py a partire da App.cpp (logica invariata).
 */

#ifndef SPIELBAR_SPIELBARAPP_H
#define SPIELBAR_SPIELBARAPP_H

#include <Application.h>
#include <Message.h>
#include <Messenger.h>
#include <SupportDefs.h>

#include "SpielBarDefs.h"
#include "SpielBarTypes.h"
#include "Emblems.h"
#include "DeskbarWindow.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SpielBar"


// -------------------------------------------------------------
// App
// -------------------------------------------------------------

// Argomenti -> stato (piano par. 2.7). Si possono combinare:
//   SpielBar --recents --apps
// Tipi (--apps --preflets --files --folders) e tag (--recents --favorites) si
// sommano in OR; per i mondi (--find --go --disks) vince l'ultimo.
// Gli argomenti sostituiscono la vista di avvio solo per questa sessione.

bool
ParseStartArgs(int32 argc, char** argv, FacetState* out);

class DeskbarApp : public BApplication {
public:
	DeskbarApp()
		: BApplication("application/x-vnd.SpielBar")
		, fHasStart(false) {}

	~DeskbarApp()
	{
		// Con l'app ancora viva (connessione all'app_server aperta): un
		// distruttore statico girerebbe dopo main() e andrebbe in crash.
		DestroyEmblems();
	}

	// L'app e' single-launch: un secondo lancio con argomento arriva qui
	// all'istanza viva. Gli argomenti non toccano mai le impostazioni.
	void ArgvReceived(int32 argc, char** argv) override
	{
		if (argc < 2)
			return;
		FacetState st;
		if (!ParseStartArgs(argc, argv, &st))
			return;

		if (fWindow.IsValid()) {
			BMessage msg(kMsgSetState);
			msg.AddInt32("world", st.world);
			msg.AddInt32("kinds", (int32)st.kindMask);
			msg.AddInt32("tag", (int32)st.tags);
			msg.AddInt32("source", kFromArgs);
			fWindow.SendMessage(&msg);
		} else {
			fStart = st;
			fHasStart = true;
		}
	}

	void ReadyToRun() override
	{
		DeskbarWindow* win = new DeskbarWindow(fHasStart ? &fStart : NULL);
		fWindow = BMessenger(win);
		win->Show();
	}

private:
	bool fHasStart;
	FacetState fStart;
	BMessenger fWindow;
};

int
main();

#endif	// SPIELBAR_SPIELBARAPP_H
