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
	status = create_directory(path.Path(), 0700);
	if (status != B_OK && status != B_FILE_EXISTS)
		return status;

	path.Append("sequence_escrow");
	fPath = path.Path();
	return B_OK;
}


status_t
EscrowStore::Set(const int32* sequence, const uint8* key, size_t keyLength)
{
	if (fPath.IsEmpty())
		return B_NO_INIT;
	if (keyLength != 32)
		return B_BAD_VALUE;

	BFile file(fPath.String(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	status_t status = file.InitCheck();
	if (status != B_OK)
		return status;

	file.SetPermissions(0600);

	ssize_t written = file.Write(sequence, kSequenceLength * sizeof(int32));
	if (written < 0)
		return (status_t)written;
	if ((size_t)written != kSequenceLength * sizeof(int32))
		return B_IO_ERROR;

	written = file.Write(key, keyLength);
	if (written < 0)
		return (status_t)written;
	if ((size_t)written != keyLength)
		return B_IO_ERROR;

	return B_OK;
}


status_t
EscrowStore::Get(int32* outSequence, uint8* outKey, size_t keyLength)
{
	if (fPath.IsEmpty())
		return B_NO_INIT;
	if (keyLength != 32)
		return B_BAD_VALUE;

	BFile file(fPath.String(), B_READ_ONLY);
	status_t status = file.InitCheck();
	if (status != B_OK)
		return status;

	ssize_t readBytes = file.Read(outSequence, kSequenceLength * sizeof(int32));
	if (readBytes < 0)
		return (status_t)readBytes;
	if ((size_t)readBytes != kSequenceLength * sizeof(int32))
		return B_IO_ERROR;

	readBytes = file.Read(outKey, keyLength);
	if (readBytes < 0)
		return (status_t)readBytes;
	if ((size_t)readBytes != keyLength)
		return B_IO_ERROR;

	return B_OK;
}


status_t
EscrowStore::Clear()
{
	if (fPath.IsEmpty())
		return B_NO_INIT;

	BEntry entry(fPath.String());
	if (!entry.Exists())
		return B_OK;

	return entry.Remove();
}


bool
EscrowStore::HasEscrow()
{
	if (fPath.IsEmpty())
		return false;

	BEntry entry(fPath.String());
	return entry.Exists();
}
