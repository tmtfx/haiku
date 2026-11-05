#ifndef _MANUAL_CHECKSUM_WINDOW_H
#define _MANUAL_CHECKSUM_WINDOW_H


#include <String.h>
#include <Window.h>


class BButton;
class BTextControl;


// Lets the user bless a package installed from a local file (no
// integrity.hpkg.sha256 - unlike a repository download or a build output,
// a local file has no trust anchor to derive one from) with a checksum
// they trust, before the install that triggered this window proceeds.
// Deliberately NOT auto-filled by default: "Calculate Checksum" only
// shows what the file's hash actually is right now, for the user to
// compare against a value they got from elsewhere (the vendor's site,
// a signed release page, ...) before deciding to save it - this is a
// human trust decision, not an automatic self-certification.
//
// Blocking, same idiom as ResultWindow/ProblemWindow in this directory:
// construct, then call Go() on the calling thread - it shows the window
// and blocks (via a semaphore, not the calling thread's own message
// loop) until the user answers, then deletes itself.
class ManualChecksumWindow : public BWindow {
public:
								ManualChecksumWindow(const char* packageName,
									const char* packagePath);
	virtual						~ManualChecksumWindow();

	// Blocks until the user answers. Returns true and fills _checksum
	// with a validated SHA256 hex string if they chose to save one;
	// returns false (leaving _checksum untouched) if they cancelled.
			bool				Go(BString& _checksum);

	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				QuitRequested();

private:
			void				_CalculateChecksum();
			void				_Save();

			BString				fPackagePath;
			BString				fChecksumResult;
			sem_id				fDoneSemaphore;
			bool				fClientWaiting;
			bool				fAccepted;
			BTextControl*		fChecksumControl;
			BButton*			fCalculateButton;
			BButton*			fCancelButton;
			BButton*			fSaveButton;
};


#endif // _MANUAL_CHECKSUM_WINDOW_H
