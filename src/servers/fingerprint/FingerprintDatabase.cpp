/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include "FingerprintDatabase.h"

#include <stdlib.h>
#include <time.h>

#include <new>

#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <Message.h>
#include <Path.h>

#include "NbisMatcher.h"


// bozorth3 match scores aren't on a fixed 0-1000 scale like the earlier
// placeholder correlation matcher - this is a starting point commonly
// cited for low-false-accept-rate operation in NBIS/FVC-style
// evaluations, NOT validated against this specific sensor. Expect to
// retune empirically against real captures, same as the placeholder
// matcher's threshold was.
static const int32 kMatchThreshold = 40;


FingerprintDatabase::FingerprintDatabase()
{
}


FingerprintDatabase::~FingerprintDatabase()
{
}


status_t
FingerprintDatabase::Init()
{
	BPath path;
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &path);
	if (status != B_OK)
		return status;

	path.Append("fingerprint_server");
	path.Append("templates");

	BDirectory dir;
	status = create_directory(path.Path(), 0700);
	if (status != B_OK && status != B_FILE_EXISTS)
		return status;

	fTemplatesDir = path.Path();
	return B_OK;
}


status_t
FingerprintDatabase::_EntryPath(int32 fingerId, BString& path)
{
	if (fTemplatesDir.IsEmpty())
		return B_NO_INIT;
	path = fTemplatesDir;
	path << "/" << fingerId << ".tmpl";
	return B_OK;
}


int32
FingerprintDatabase::_NextFingerId()
{
	BDirectory dir(fTemplatesDir.String());
	int32 maxId = 0;
	BEntry entry;
	while (dir.GetNextEntry(&entry) == B_OK) {
		char name[B_FILE_NAME_LENGTH];
		if (entry.GetName(name) != B_OK)
			continue;
		int32 id = atol(name);
		if (id > maxId)
			maxId = id;
	}
	return maxId + 1;
}


status_t
FingerprintDatabase::SaveEnrollment(const char* label,
	const uint8* const* samples, int32 sampleCount, int32 imageWidth,
	int32 imageHeight, int32* outFingerId)
{
	if (fTemplatesDir.IsEmpty())
		return B_NO_INIT;
	if (sampleCount <= 0)
		return B_BAD_VALUE;

	BMessage record;
	record.AddString("label", label);
	record.AddInt64("createdAt", (int64)time(NULL));
	record.AddInt32("imageWidth", imageWidth);
	record.AddInt32("imageHeight", imageHeight);

	int32 usableTemplates = 0;
	for (int32 i = 0; i < sampleCount; i++) {
		uint8* nbisTemplate = NULL;
		size_t templateSize = 0;
		int32 minutiaeCount = 0;
		status_t status = NbisMatcher::ExtractMinutiae(samples[i], imageWidth,
			imageHeight, &nbisTemplate, &templateSize, &minutiaeCount);
		if (status != B_OK || minutiaeCount == 0) {
			delete[] nbisTemplate;
			continue; // a blurry/partial capture mindtct found nothing in -
			          // skip it rather than fail the whole enrollment
		}

		record.AddData("minutiaeTemplate", B_RAW_TYPE, nbisTemplate,
			templateSize, false);
		delete[] nbisTemplate;
		usableTemplates++;
	}

	if (usableTemplates == 0)
		return B_ERROR; // none of the captures yielded any minutiae at all

	record.AddInt32("sampleCount", usableTemplates);

	int32 fingerId = _NextFingerId();
	BString path;
	status_t status = _EntryPath(fingerId, path);
	if (status != B_OK)
		return status;

	BFile file(path.String(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	status = file.InitCheck();
	if (status != B_OK)
		return status;

	status = record.Flatten(&file);
	if (status != B_OK)
		return status;

	*outFingerId = fingerId;
	return B_OK;
}


status_t
FingerprintDatabase::Delete(int32 fingerId)
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


status_t
FingerprintDatabase::List(BMessage* reply)
{
	if (fTemplatesDir.IsEmpty())
		return B_NO_INIT;

	BDirectory dir(fTemplatesDir.String());
	BEntry entry;
	while (dir.GetNextEntry(&entry) == B_OK) {
		char name[B_FILE_NAME_LENGTH];
		if (entry.GetName(name) != B_OK)
			continue;
		int32 fingerId = atol(name);

		BPath path;
		if (entry.GetPath(&path) != B_OK)
			continue;

		BFile file(path.Path(), B_READ_ONLY);
		if (file.InitCheck() != B_OK)
			continue;

		BMessage record;
		if (record.Unflatten(&file) != B_OK)
			continue;

		BString label;
		int64 createdAt = 0;
		record.FindString("label", &label);
		record.FindInt64("createdAt", &createdAt);

		reply->AddInt32("fingerId", fingerId);
		reply->AddString("label", label);
		reply->AddInt64("createdAt", createdAt);
	}

	return B_OK;
}


status_t
FingerprintDatabase::Match(const uint8* probeImage, int32 imageWidth,
	int32 imageHeight, int32* outFingerId, int32* outScore)
{
	if (fTemplatesDir.IsEmpty())
		return B_NO_INIT;

	uint8* probeTemplate = NULL;
	size_t probeSize = 0;
	int32 probeMinutiaeCount = 0;
	status_t status = NbisMatcher::ExtractMinutiae(probeImage, imageWidth,
		imageHeight, &probeTemplate, &probeSize, &probeMinutiaeCount);
	if (status != B_OK)
		return status;
	if (probeMinutiaeCount == 0) {
		delete[] probeTemplate;
		*outFingerId = -1;
		*outScore = 0;
		return B_NAME_NOT_FOUND;
	}

	int32 bestFingerId = -1;
	int32 bestScore = 0;

	BDirectory dir(fTemplatesDir.String());
	BEntry entry;
	while (dir.GetNextEntry(&entry) == B_OK) {
		char name[B_FILE_NAME_LENGTH];
		if (entry.GetName(name) != B_OK)
			continue;
		int32 fingerId = atol(name);

		BPath path;
		if (entry.GetPath(&path) != B_OK)
			continue;

		BFile file(path.Path(), B_READ_ONLY);
		if (file.InitCheck() != B_OK)
			continue;

		BMessage record;
		if (record.Unflatten(&file) != B_OK)
			continue;

		int32 sampleCount = 0;
		record.FindInt32("sampleCount", &sampleCount);

		for (int32 i = 0; i < sampleCount; i++) {
			const void* storedData;
			ssize_t storedSize;
			if (record.FindData("minutiaeTemplate", B_RAW_TYPE, i,
					&storedData, &storedSize) != B_OK) {
				continue;
			}

			int32 score = NbisMatcher::Compare(probeTemplate, probeSize,
				(const uint8*)storedData, (size_t)storedSize);
			if (score > bestScore) {
				bestScore = score;
				bestFingerId = fingerId;
			}
		}
	}

	delete[] probeTemplate;

	*outScore = bestScore;
	if (bestFingerId < 0 || bestScore < kMatchThreshold) {
		*outFingerId = -1;
		return B_NAME_NOT_FOUND;
	}

	*outFingerId = bestFingerId;
	return B_OK;
}
