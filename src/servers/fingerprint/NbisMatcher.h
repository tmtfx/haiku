/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef NBIS_MATCHER_H
#define NBIS_MATCHER_H


#include <SupportDefs.h>


// Thin C++ adapter around the vendored NBIS mindtct (minutiae extraction)
// and bozorth3 (minutiae matching) libraries - see
// src/libs/fingerprint/nbis/README.haiku for where that code came from.
// Replaces the earlier placeholder downsampled-correlation matcher with
// real minutiae-based matching.
class NbisMatcher {
public:
	// Extracts minutiae from a raw 8bpp grayscale image and serializes
	// them into a compact template buffer the caller owns (delete[] it).
	// Format: int32 count, followed by count * (int32 x, int32 y,
	// int32 thetaDegrees), capped at 200 minutiae (bozorth3's own hard
	// limit - MAX_BOZORTH_MINUTIAE). Returns B_OK even if very few
	// minutiae were found (that's a quality judgment for the caller via
	// outMinutiaeCount, not a hard failure) - only returns an error if
	// mindtct itself fails on the image.
	static	status_t			ExtractMinutiae(const uint8* image,
									int32 width, int32 height,
									uint8** outTemplate, size_t* outSize,
									int32* outMinutiaeCount);

	// Compares two serialized templates (as produced by ExtractMinutiae)
	// and returns the bozorth3 match score - empirically, scores above
	// roughly 40 are a plausible same-finger match by bozorth3's usual
	// convention, but this needs tuning against real captures from
	// whichever sensor is in use, the same way the placeholder
	// correlation matcher's threshold did.
	static	int32				Compare(const uint8* templateA,
									size_t sizeA, const uint8* templateB,
									size_t sizeB);
};


#endif // NBIS_MATCHER_H
