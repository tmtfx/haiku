/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include "FingerprintDatabase.h"

#include <math.h>
#include <stdlib.h>
#include <time.h>

#include <new>

#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <Message.h>
#include <Path.h>

#include "Uru4000Protocol.h"

using namespace Uru4000;


// Downsample factor for the placeholder correlation matcher - keeps the
// search window cheap (full-res images are 384x290).
static const int kDownsample = 4;
static const int kSmallWidth = kImageWidth / kDownsample;
static const int kSmallHeight = kImageHeight / kDownsample;
static const int kSearchRange = 8;	// +/- pixels, in downsampled units
static const int32 kMatchThreshold = 700;	// out of 1000, empirical placeholder


static void
DownsampleImage(const uint8* image, uint8* out)
{
	for (int y = 0; y < kSmallHeight; y++) {
		for (int x = 0; x < kSmallWidth; x++) {
			uint32 sum = 0;
			for (int dy = 0; dy < kDownsample; dy++) {
				for (int dx = 0; dx < kDownsample; dx++) {
					int sy = y * kDownsample + dy;
					int sx = x * kDownsample + dx;
					sum += image[sy * kImageWidth + sx];
				}
			}
			out[y * kSmallWidth + x] = (uint8)(sum / (kDownsample * kDownsample));
		}
	}
}


// Normalized cross-correlation at a single (dx, dy) shift, over the
// overlapping region only. Returns a value roughly in [-1000, 1000].
static int32
CorrelateAt(const uint8* a, const uint8* b, int dx, int dy)
{
	int x0 = dx > 0 ? dx : 0;
	int x1 = dx < 0 ? kSmallWidth + dx : kSmallWidth;
	int y0 = dy > 0 ? dy : 0;
	int y1 = dy < 0 ? kSmallHeight + dy : kSmallHeight;
	if (x1 <= x0 || y1 <= y0)
		return -1000;

	int64 sumA = 0, sumB = 0, sumAB = 0, sumA2 = 0, sumB2 = 0;
	int64 n = 0;
	for (int y = y0; y < y1; y++) {
		for (int x = x0; x < x1; x++) {
			int32 va = a[y * kSmallWidth + x];
			int32 vb = b[(y - dy) * kSmallWidth + (x - dx)];
			sumA += va;
			sumB += vb;
			sumAB += va * vb;
			sumA2 += va * va;
			sumB2 += vb * vb;
			n++;
		}
	}
	if (n == 0)
		return -1000;

	double meanA = (double)sumA / n;
	double meanB = (double)sumB / n;
	double cov = (double)sumAB / n - meanA * meanB;
	double varA = (double)sumA2 / n - meanA * meanA;
	double varB = (double)sumB2 / n - meanB * meanB;
	double denom = varA * varB;
	if (denom <= 0.0)
		return -1000;

	double corr = cov / sqrt(denom);
	return (int32)(corr * 1000.0);
}


static int32
BestCorrelation(const uint8* smallA, const uint8* smallB)
{
	int32 best = -1000;
	for (int dy = -kSearchRange; dy <= kSearchRange; dy++) {
		for (int dx = -kSearchRange; dx <= kSearchRange; dx++) {
			int32 score = CorrelateAt(smallA, smallB, dx, dy);
			if (score > best)
				best = score;
		}
	}
	return best;
}


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
	const uint8* const* samples, int32 sampleCount, int32* outFingerId)
{
	if (fTemplatesDir.IsEmpty())
		return B_NO_INIT;
	if (sampleCount <= 0)
		return B_BAD_VALUE;

	int32 fingerId = _NextFingerId();

	BMessage record;
	record.AddString("label", label);
	record.AddInt64("createdAt", (int64)time(NULL));
	record.AddInt32("sampleCount", sampleCount);
	for (int32 i = 0; i < sampleCount; i++) {
		record.AddData("sample", B_RAW_TYPE, samples[i],
			kImageWidth * kImageHeight, false);
	}

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
FingerprintDatabase::Match(const uint8* probeImage, int32* outFingerId,
	int32* outScore)
{
	if (fTemplatesDir.IsEmpty())
		return B_NO_INIT;

	uint8 smallProbe[kSmallWidth * kSmallHeight];
	DownsampleImage(probeImage, smallProbe);

	int32 bestFingerId = -1;
	int32 bestScore = -1000;

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
			const void* sampleData;
			ssize_t sampleSize;
			if (record.FindData("sample", B_RAW_TYPE, i, &sampleData,
					&sampleSize) != B_OK) {
				continue;
			}
			if ((size_t)sampleSize != (size_t)(kImageWidth * kImageHeight))
				continue;

			uint8 smallSample[kSmallWidth * kSmallHeight];
			DownsampleImage((const uint8*)sampleData, smallSample);

			int32 score = BestCorrelation(smallProbe, smallSample);
			if (score > bestScore) {
				bestScore = score;
				bestFingerId = fingerId;
			}
		}
	}

	*outScore = bestScore;
	if (bestFingerId < 0 || bestScore < kMatchThreshold) {
		*outFingerId = -1;
		return B_NAME_NOT_FOUND;
	}

	*outFingerId = bestFingerId;
	return B_OK;
}
