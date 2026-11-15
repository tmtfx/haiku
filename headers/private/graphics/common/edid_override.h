/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef _EDID_OVERRIDE_H
#define _EDID_OVERRIDE_H


/*!	Kernel-side helper that lets a graphics driver replace the EDID it would
	otherwise obtain from the boot loader (VESA_EDID_BOOT_INFO), the video BIOS
	or DDC with an edid1_info previously saved by the Screen preferences
	("Dump EDID" button).

	The file to load is named by the "edid" key of the driver settings file
	(~/config/settings/kernel/drivers/<driver>.settings):

		edid /boot/home/config/non-packaged/settings/edid/dumped_edid.bin

	The file must contain a decoded edid1_info structure (as written by
	"Dump EDID"), NOT a raw 128 byte EDID block.

	This must only be called once the boot volume is mounted, i.e. from the
	driver's open hook or later, never from init_hardware()/early boot.
*/


#include <KernelExport.h>
#include <driver_settings.h>
#include <edid.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


#define EDID_OVERRIDE_SETTINGS_KEY "edid"


static inline status_t
load_edid_override(const char* settingsName, edid1_info* edid)
{
	static const uint8 kRawEDIDHeader[8]
		= { 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00 };

	char path[B_PATH_NAME_LENGTH];
	void* handle;
	const char* value;
	edid1_info* buffer;
	ssize_t bytesRead;
	status_t status = B_OK;
	int fd;

	if (settingsName == NULL || edid == NULL)
		return B_BAD_VALUE;

	handle = load_driver_settings(settingsName);
	if (handle == NULL)
		return B_ENTRY_NOT_FOUND;

	value = get_driver_parameter(handle, EDID_OVERRIDE_SETTINGS_KEY, NULL,
		NULL);
	if (value == NULL || value[0] == '\0') {
		unload_driver_settings(handle);
		return B_ENTRY_NOT_FOUND;
	}
	strlcpy(path, value, sizeof(path));
	unload_driver_settings(handle);

	fd = open(path, O_RDONLY);
	if (fd < 0) {
		status = errno;
		dprintf("%s: EDID override: cannot open \"%s\": %s\n", settingsName,
			path, strerror(status));
		return status;
	}

	buffer = (edid1_info*)malloc(sizeof(edid1_info));
	if (buffer == NULL) {
		close(fd);
		return B_NO_MEMORY;
	}

	bytesRead = read(fd, buffer, sizeof(edid1_info));
	close(fd);

	if (bytesRead >= (ssize_t)sizeof(kRawEDIDHeader)
		&& memcmp(buffer, kRawEDIDHeader, sizeof(kRawEDIDHeader)) == 0) {
		dprintf("%s: EDID override: \"%s\" is a raw EDID block, an edid1_info "
			"dumped by the Screen preferences is required\n", settingsName,
			path);
		status = B_BAD_DATA;
	} else if (bytesRead != (ssize_t)sizeof(edid1_info)) {
		dprintf("%s: EDID override: \"%s\" has wrong size (%" B_PRIdSSIZE
			", expected %" B_PRIuSIZE ")\n", settingsName, path, bytesRead,
			sizeof(edid1_info));
		status = bytesRead < 0 ? (status_t)errno : B_BAD_DATA;
	} else if (buffer->version.version != 1) {
		dprintf("%s: EDID override: \"%s\" has unsupported EDID version %u\n",
			settingsName, path, buffer->version.version);
		status = B_BAD_DATA;
	}

	if (status == B_OK) {
		memcpy(edid, buffer, sizeof(edid1_info));
		dprintf("%s: EDID override loaded from \"%s\" (%s, EDID %u.%u)\n",
			settingsName, path, edid->vendor.manufacturer,
			edid->version.version, edid->version.revision);
	}

	free(buffer);
	return status;
}


#endif	/* _EDID_OVERRIDE_H */
