/*
 * Copyright 2008-2011, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Michael Pfeiffer <laplace@users.sourceforge.net>
 */
#ifndef BOOT_MENU_H
#define BOOT_MENU_H


#include <File.h>
#include <Message.h>
#include <ObjectList.h>


class BootDrive;


/*	Setting BMessage Format:

	"partition" array of BMessage:
		"show" bool (flag if entry should be added to boot menu)
		"name" String (the name as shown in boot menu)
		"type" String (short name of file system: bfs, dos)
		"path" String (path to partition in /dev/...)
		"size" long (size of partition in bytes)

	"bootFile" array of BMessage (a bootable disk image file, living on any
	writable filesystem, that is added to the boot menu like a partition;
	see LegacyBootMenu::Install()). Named "bootFile", not "file", because
	"file" is already used elsewhere in this settings message for the MBR
	backup file path (see BootManagerController.h):
		"show" bool (flag if entry should be added to boot menu)
		"name" String (the name as shown in boot menu)
		"path" String (path to the image file; this is the only durable
			key kept across runs -- the file's on-disk LBA is re-resolved
			from this path every time Install() runs, since the file may
			move or be rewritten between runs)
		"partitionOffset" long long, optional (byte offset, within the
			image file itself, of the actual bootable BFS partition;
			defaults to 0, meaning the whole file is directly one BFS
			volume. Must be set to the inner partition's own offset when
			the file was instead given its own inner partition table --
			e.g. "Intel" -- with the BFS partition created inside it, the
			normal result of partitioning a registered file-disk-device
			in DriveSetup like any other disk; see
			LegacyBootMenu::_ResolveFileBootLocation())

	"defaultBootFile" long, optional (index into the "bootFile" array
	designating the default boot menu entry; when present and >= 0, it
	takes precedence over "defaultPartition")
*/


class BootMenu {
public:
								BootMenu() {}
	virtual						~BootMenu() {}

	virtual	bool				IsInstalled(const BootDrive& drive) = 0;
	virtual	status_t			CanBeInstalled(const BootDrive& drive) = 0;

	virtual	status_t			CollectPartitions(const BootDrive& drive,
									BMessage& settings) = 0;

	virtual	status_t			Install(const BootDrive& drive,
									BMessage& settings) = 0;
	virtual	status_t			SaveMasterBootRecord(BMessage* settings,
									BFile* file) = 0;
	virtual	status_t			RestoreMasterBootRecord(BMessage* settings,
									BFile* file) = 0;

	// Converts the specified text into a text as it will be shown
	// in the boot menu.
	virtual	status_t			GetDisplayText(const char* text,
									BString& displayText) = 0;
};

typedef BObjectList<BootMenu> BootMenuList;


#endif	// BOOT_MENU_H
