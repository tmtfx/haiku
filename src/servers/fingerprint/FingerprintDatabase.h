/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef FINGERPRINT_DATABASE_H
#define FINGERPRINT_DATABASE_H


#include <String.h>
#include <SupportDefs.h>


class BMessage;


// Stores enrolled fingerprint minutiae templates (extracted via NBIS
// mindtct, one file per finger under the user's private settings
// directory) and matches a freshly captured probe image against them via
// NBIS bozorth3 - see src/libs/fingerprint/nbis/README.haiku and
// NbisMatcher.h. Raw images are never written to disk, only the
// extracted minutiae (a much smaller, less sensitive representation).
class FingerprintDatabase {
public:
								FingerprintDatabase();
								~FingerprintDatabase();

			status_t			Init();

			// Allocates a new finger id and persists `sampleCount` raw
			// images (each imageWidth * imageHeight bytes, geometry as
			// reported by whichever FingerprintDevice captured them -
			// stored alongside the samples so Match() can tell records
			// from a different device model/geometry apart) under that id
			// with the given label. Returns the new id via outFingerId.
			status_t			SaveEnrollment(const char* label,
									const uint8* const* samples,
									int32 sampleCount, int32 imageWidth,
									int32 imageHeight, int32* outFingerId);

			status_t			Delete(int32 fingerId);

			// Fills reply with parallel "fingerId"/"label"/"createdAt"
			// arrays for every enrolled finger.
			status_t			List(BMessage* reply);

			// Compares probeImage (imageWidth * imageHeight bytes) against
			// every stored sample of every enrolled finger whose stored
			// geometry matches. On a match above threshold returns B_OK
			// with outFingerId/outScore set; otherwise B_NAME_NOT_FOUND.
			status_t			Match(const uint8* probeImage,
									int32 imageWidth, int32 imageHeight,
									int32* outFingerId, int32* outScore);

private:
			BString				fTemplatesDir;

			status_t			_EntryPath(int32 fingerId, BString& path);
			int32				_NextFingerId();
};


#endif // FINGERPRINT_DATABASE_H
