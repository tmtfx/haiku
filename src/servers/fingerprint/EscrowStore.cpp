/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include "EscrowStore.h"

#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <Path.h>


EscrowStore::EscrowStore()
{
}


EscrowStore::~EscrowStore()
{
}


status_t
EscrowStore::Init()
{
	BPath path;
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &path);
	if (status != B_OK)
		return status;

	path.Append("fingerprint_server");
	path.Append("escrow");

	status = create_directory(path.Path(), 0700);
	if (status != B_OK && status != B_FILE_EXISTS)
		return status;

	fEscrowDir = path.Path();
	return B_OK;
}


status_t
EscrowStore::_EntryPath(int32 fingerId, BString& path)
{
	if (fEscrowDir.IsEmpty())
		return B_NO_INIT;
	path = fEscrowDir;
	path << "/" << fingerId << ".key";
	return B_OK;
}


status_t
EscrowStore::Set(int32 fingerId, const uint8* key, size_t keyLength)
{
	BString path;
	status_t status = _EntryPath(fingerId, path);
	if (status != B_OK)
		return status;

	BFile file(path.String(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	status = file.InitCheck();
	if (status != B_OK)
		return status;

	file.SetPermissions(0600);

	ssize_t written = file.Write(key, keyLength);
	if (written < 0)
		return (status_t)written;
	if ((size_t)written != keyLength)
		return B_IO_ERROR;

	return B_OK;
}


status_t
EscrowStore::Get(int32 fingerId, uint8* outKey, size_t keyLength)
{
	BString path;
	status_t status = _EntryPath(fingerId, path);
	if (status != B_OK)
		return status;

	BFile file(path.String(), B_READ_ONLY);
	status = file.InitCheck();
	if (status != B_OK)
		return status;

	ssize_t readBytes = file.Read(outKey, keyLength);
	if (readBytes < 0)
		return (status_t)readBytes;
	if ((size_t)readBytes != keyLength)
		return B_IO_ERROR;

	return B_OK;
}


status_t
EscrowStore::Delete(int32 fingerId)
{
	BString path;
	status_t status = _EntryPath(fingerId, path);
	if (status != B_OK)
		return status;

	BEntry entry(path.String());
	if (!entry.Exists())
		return B_ENTRY_NOT_FOUND;

	return entry.Remove();
}
