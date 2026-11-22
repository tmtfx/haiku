/*
 * Copyright 2008-2010, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Michael Pfeiffer <laplace@users.sourceforge.net>
 * 		Axel Dörfler <axeld@pinc-software.de>
 */


#include "BootManagerWindow.h"

#include <Application.h>
#include <Catalog.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "BootManager"


static const char* kSignature = "application/x-vnd.Haiku-BootManager";


class BootManager : public BApplication {
public:
								BootManager(const char* diskPath,
									const char* filePath);

	virtual void				ReadyToRun();

private:
			const char*			fDiskPath;
			const char*			fFilePath;
};


BootManager::BootManager(const char* diskPath, const char* filePath)
	:
	BApplication(kSignature),
	fDiskPath(diskPath),
	fFilePath(filePath)
{
}


void
BootManager::ReadyToRun()
{
	BootManagerWindow* window = new BootManagerWindow(fDiskPath, fFilePath);
	window->Show();
}


//	#pragma mark -


int
main(int argc, char** argv)
{
	// When launched by Installer with a known install target, argv[1] is
	// the disk to install the boot menu on, and argv[2] is a bootable disk
	// image file to add to the boot menu (see
	// InstallerWindow::_LaunchBootManager()). With no arguments, the
	// wizard behaves exactly as before, letting the user pick a disk.
	const char* diskPath = argc > 1 ? argv[1] : NULL;
	const char* filePath = argc > 2 ? argv[2] : NULL;

	BootManager application(diskPath, filePath);
	application.Run();

	return 0;
}
