/*
 * Copyright 2026, Pirati del Frico. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Copilot CLI
 *		tmtfx
 *
 * Implementation of HPKG integrity writer
 */


#include "HPKGIntegrityWriter.h"

#include <stdio.h>
#include <ctime>
#include <cstring>

#include <DateTime.h>
#include <File.h>
#include <Node.h>
#include <TypeConstants.h>
#include <fs_attr.h>

#include "HPKGIntegrityConstants.h"

using namespace BPackageKit::BPrivate;


// ==============================================================================
// Public API
// ==============================================================================

status_t
HPKGIntegrityWriter::WriteChecksum(const entry_ref& entry,
	const BString& checksum, const char* source)
{
	// Validate checksum format
	if (!_IsValidChecksum(checksum)) {
		fprintf(stderr, "HPKGIntegrityWriter::WriteChecksum(): "
			"Invalid checksum format: %s\n", checksum.String());
		return B_BAD_VALUE;
	}

	// Best-effort: create the BFS indices the first time we write these
	// attributes, so fsck_hpkgs's `query` command can use a real BQuery
	// instead of always falling back to a directory scan. Not fatal if
	// it fails (e.g. non-BFS volume).
	_HPKGEnsureIndex(entry.device, HPKG_ATTR_CHECKSUM);
	_HPKGEnsureIndex(entry.device, HPKG_ATTR_VERIFIED);
	_HPKGEnsureIndex(entry.device, HPKG_ATTR_SOURCE);

	// Write to extended attribute
	status_t status = _WriteAttribute(entry, HPKG_ATTR_CHECKSUM,
		checksum.String());

	if (status == B_OK && source != NULL)
		_WriteAttribute(entry, HPKG_ATTR_SOURCE, source);

	if (status == B_OK) {
		fprintf(stderr, "HPKGIntegrityWriter: Wrote checksum for %s\n",
			entry.name);
	} else {
		fprintf(stderr, "HPKGIntegrityWriter: Failed to write checksum "
			"for %s: %s\n", entry.name, strerror(status));
	}

	return status;
}


status_t
HPKGIntegrityWriter::ReadChecksum(const entry_ref& entry,
	BString& outChecksum)
{
	return _ReadAttribute(entry, HPKG_ATTR_CHECKSUM, outChecksum);
}


status_t
HPKGIntegrityWriter::UpdateVerificationTimestamp(const entry_ref& entry,
	const BString& timestamp)
{
	BString ts = timestamp;
	if (ts.IsEmpty())
		ts = _GetCurrentTimestamp();

	status_t status = _WriteAttribute(entry, HPKG_ATTR_VERIFIED,
		ts.String());

	if (status != B_OK) {
		fprintf(stderr, "HPKGIntegrityWriter: Failed to update "
			"verification timestamp for %s: %s\n",
			entry.name, strerror(status));
	}

	return status;
}


// ==============================================================================
// Private Helpers
// ==============================================================================

status_t
HPKGIntegrityWriter::_WriteAttribute(const entry_ref& entry,
	const char* attributeName, const char* value)
{
	if (entry.name == NULL || attributeName == NULL || value == NULL)
		return B_BAD_VALUE;

	BNode node(&entry);
	status_t status = node.InitCheck();
	if (status != B_OK)
		return status;

	size_t valueLen = strlen(value);
	ssize_t written = node.WriteAttr(attributeName, B_STRING_TYPE, 0,
		value, valueLen + 1);  // +1 for null terminator

	if (written < 0)
		return (status_t)written;

	if (written != (ssize_t)(valueLen + 1))
		return B_IO_ERROR;

	return B_OK;
}


status_t
HPKGIntegrityWriter::_ReadAttribute(const entry_ref& entry,
	const char* attributeName, BString& outValue)
{
	if (entry.name == NULL || attributeName == NULL)
		return B_BAD_VALUE;

	BNode node(&entry);
	status_t status = node.InitCheck();
	if (status != B_OK)
		return status;

	// Get attribute info to determine size
	attr_info attrInfo;
	status = node.GetAttrInfo(attributeName, &attrInfo);
	if (status != B_OK) {
		// Attribute doesn't exist - not necessarily an error
		return B_ENTRY_NOT_FOUND;
	}

	// Read the attribute value
	char buffer[256];  // Safe for checksum (64 chars) + timestamp (31 chars)
	if (attrInfo.size > (int32)sizeof(buffer) - 1) {
		return B_BAD_VALUE;  // Attribute too large
	}

	ssize_t bytesRead = node.ReadAttr(attributeName, B_STRING_TYPE, 0,
		buffer, attrInfo.size);

	if (bytesRead < 0)
		return (status_t)bytesRead;

	// Ensure null-terminated
	buffer[attrInfo.size] = '\0';
	outValue = buffer;

	return B_OK;
}


BString
HPKGIntegrityWriter::_GetCurrentTimestamp()
{
	time_t now = time(NULL);
	struct tm* timeinfo = gmtime(&now);
	if (timeinfo == NULL)
		return BString("unknown");

	char buffer[32];
	strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S UTC", timeinfo);
	return BString(buffer);
}


bool
HPKGIntegrityWriter::_IsValidChecksum(const BString& checksum)
{
	// Must use the constant from HPKGIntegrityConstants
	return _HPKGIsValidChecksumHex(checksum.String());
}
