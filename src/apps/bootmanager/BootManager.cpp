/*
 * Copyright 2008-2010, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Michael Pfeiffer <laplace@users.sourceforge.net>
 * 		Axel Dörfler <axeld@pinc-software.de>
 */


#include "BootManagerWindow.h"

#include <stdlib.h>

#include <Application.h>
#include <Catalog.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "BootManager"


static const char* kSignature = "application/x-vnd.Haiku-BootManager";


class BootManager : public BApplication {
public:
								BootManager(const char* diskPath,
									const char* filePath,
									off_t partitionOffsetInFile);

	virtual void				ReadyToRun();

private:
			const char*			fDiskPath;
			const char*			fFilePath;
			off_t				fPartitionOffsetInFile;
};


BootManager::BootManager(const char* diskPath, const char* filePath,
	off_t partitionOffsetInFile)
	:
	BApplication(kSignature),
	fDiskPath(diskPath),
	fFilePath(filePath),
	fPartitionOffsetInFile(partitionOffsetInFile)
{
}


void
BootManager::ReadyToRun()
{
	BootManagerWindow* window = new BootManagerWindow(fDiskPath, fFilePath,
		fPartitionOffsetInFile);
	window->Show();
}


//	#pragma mark -


int
main(int argc, char** argv)
{
	// When launched by Installer with a known install target, argv[1] is
	// the disk to install the boot menu on, argv[2] is a bootable disk
	// image file to add to the boot menu, and the optional argv[3] is the
	// byte offset of the actual BFS partition within that file, when the
	// file has its own inner partition table (see
	// InstallerWindow::_LaunchBootManager()). With no arguments, the
	// wizard behaves exactly as before, letting the user pick a disk.
	const char* diskPath = argc > 1 ? argv[1] : NULL;
	const char* filePath = argc > 2 ? argv[2] : NULL;
	off_t partitionOffsetInFile = argc > 3 ? strtoll(argv[3], NULL, 10) : 0;

	BootManager application(diskPath, filePath, partitionOffsetInFile);
	application.Run();

	return 0;
}
