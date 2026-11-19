/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include "NbisMatcher.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <new>

extern "C" {
#include <bozorth.h>
#include <lfs.h>
}


// bozorth3.c/bz_io.c reference these as extern globals, normally defined
// by the bozorth3 CLI tool's own main.c (which we don't vendor - we only
// need the library routines, not the command-line wrapper). Supply the
// NBIS-documented defaults ourselves.
extern "C" {
int m1_xyt = 0;                  // 0 = NIST internal XYT format (matches
                                  // our lfs2nist_minutia_XYT() conversion)
int max_minutiae = DEFAULT_BOZORTH_MINUTIAE;
int min_computable_minutiae = MIN_COMPUTABLE_BOZORTH_MINUTIAE;
int verbose_main = 0;
int verbose_load = 0;
int verbose_bozorth = 0;
FILE* errorfp = NULL;
}


namespace {

struct ErrorFpInit {
	ErrorFpInit() { errorfp = stderr; }
} gErrorFpInit;

} // namespace


// mindtct doesn't report the sensor's actual scan resolution (our daemon
// has no way to query that from the device either), so we assume a
// typical 500 DPI like most consumer fingerprint sensors of this class.
// This only affects mindtct's internal quality/curvature thresholds, not
// correctness - matching stays self-consistent as long as every capture
// from this device uses the same assumption.
static const double kAssumedDpi = 500.0;
static const double kPpmm = kAssumedDpi / 25.4;


status_t
NbisMatcher::ExtractMinutiae(const uint8* image, int32 width, int32 height,
	uint8** outTemplate, size_t* outSize, int32* outMinutiaeCount)
{
	*outTemplate = NULL;
	*outSize = 0;
	*outMinutiaeCount = 0;

	MINUTIAE* minutiae = NULL;
	int* qualityMap = NULL;
	int* directionMap = NULL;
	int* lowContrastMap = NULL;
	int* lowFlowMap = NULL;
	int* highCurveMap = NULL;
	int mapWidth = 0, mapHeight = 0;
	unsigned char* binarized = NULL;
	int binWidth = 0, binHeight = 0, binDepth = 0;

	// get_minutiae()'s signature isn't const-correct, but it doesn't
	// modify the input image.
	uint8* mutableImage = const_cast<uint8*>(image);

	int result = get_minutiae(&minutiae, &qualityMap, &directionMap,
		&lowContrastMap, &lowFlowMap, &highCurveMap, &mapWidth, &mapHeight,
		&binarized, &binWidth, &binHeight, &binDepth, mutableImage,
		(int)width, (int)height, 8, kPpmm, &lfsparms_V2);

	if (result != 0)
		return B_ERROR;

	int32 count = minutiae->num;
	if (count > MAX_BOZORTH_MINUTIAE)
		count = MAX_BOZORTH_MINUTIAE;

	size_t size = sizeof(int32) + (size_t)count * 3 * sizeof(int32);
	uint8* buffer = new(std::nothrow) uint8[size];
	if (buffer != NULL) {
		int32* out = (int32*)buffer;
		out[0] = count;
		for (int32 i = 0; i < count; i++) {
			int ox, oy, ot;
			lfs2nist_minutia_XYT(&ox, &oy, &ot, minutiae->list[i],
				(int)width, (int)height);
			out[1 + i * 3 + 0] = ox;
			out[1 + i * 3 + 1] = oy;
			out[1 + i * 3 + 2] = ot;
		}
	}

	free(qualityMap);
	free(directionMap);
	free(lowContrastMap);
	free(lowFlowMap);
	free(highCurveMap);
	free(binarized);
	free_minutiae(minutiae);

	if (buffer == NULL)
		return B_NO_MEMORY;

	*outTemplate = buffer;
	*outSize = size;
	*outMinutiaeCount = count;
	return B_OK;
}


static void
DeserializeTemplate(const uint8* data, size_t size, xyt_struct* xyt)
{
	memset(xyt, 0, sizeof(*xyt));
	if (data == NULL || size < sizeof(int32))
		return;

	const int32* in = (const int32*)data;
	int32 count = in[0];
	if (count < 0)
		count = 0;
	if (count > MAX_BOZORTH_MINUTIAE)
		count = MAX_BOZORTH_MINUTIAE;

	size_t needed = sizeof(int32) + (size_t)count * 3 * sizeof(int32);
	if (needed > size)
		count = 0; // malformed/truncated template - treat as empty

	xyt->nrows = count;
	for (int32 i = 0; i < count; i++) {
		xyt->xcol[i] = in[1 + i * 3 + 0];
		xyt->ycol[i] = in[1 + i * 3 + 1];
		xyt->thetacol[i] = in[1 + i * 3 + 2];
	}
}


int32
NbisMatcher::Compare(const uint8* templateA, size_t sizeA,
	const uint8* templateB, size_t sizeB)
{
	xyt_struct probe;
	xyt_struct gallery;
	DeserializeTemplate(templateA, sizeA, &probe);
	DeserializeTemplate(templateB, sizeB, &gallery);

	if (probe.nrows == 0 || gallery.nrows == 0)
		return 0;

	int probeLen = bozorth_probe_init(&probe);
	return bozorth_to_gallery(probeLen, &probe, &gallery);
}
