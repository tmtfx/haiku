/*
 * Copyright 2026, Pirati del Frico. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Copilot CLI
 *		tmtfx
 *
 * Package verification implementation
 *
 * NOTE: When building on native Haiku, HPKGIntegrityConstants will be linked
 * from libpackage.so automatically. The manual object file linking in Jamfile
 * is a workaround for cross-compilation on non-Haiku systems.
 */

#include "PackageVerifier.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include <File.h>
#include <Node.h>
#include <Path.h>
#include <TypeConstants.h>
#include <fs_attr.h>
#include <crypto/BCrypto.h>

#include "HPKGIntegrityConstants.h"

using namespace BPackageKit::BPrivate;


// ==============================================================================
// PackageVerifier
// ==============================================================================

PackageVerifier::PackageVerifier()
{
}


PackageVerifier::~PackageVerifier()
{
}


status_t
PackageVerifier::VerifyPackage(const char* filePath,
	PackageVerificationResult* outResult)
{
	if (filePath == NULL || outResult == NULL)
		return B_BAD_VALUE;
	
	// Reset via assignment, not memset: BString's copy-assignment is
	// non-trivial (it manages a reference-counted buffer), so zeroing
	// the struct's raw memory would corrupt any BString field that was
	// already properly constructed.
	*outResult = PackageVerificationResult();
	outResult->filePath = filePath;
	
	// Extract filename from path
	const char* filename = strrchr(filePath, '/');
	outResult->filename = (filename != NULL) ? filename + 1 : filePath;
	
	// Step 1: Read existing checksum attribute
	status_t status = _ReadAttribute(filePath, HPKG_ATTR_CHECKSUM,
		outResult->attributedChecksum);
	outResult->attributeExists = (status == B_OK);
	
	if (outResult->attributeExists) {
		_ReadAttribute(filePath, HPKG_ATTR_VERIFIED,
			outResult->attributedVerified);
		_ReadAttribute(filePath, HPKG_ATTR_SOURCE,
			outResult->attributedSource);
	}
	
	// Step 2: Calculate SHA256 from file contents
	outResult->calculationStatus = _CalculateChecksum(filePath,
		outResult->calculatedChecksum);
	
	if (outResult->calculationStatus != B_OK) {
		outResult->isValid = false;
		outResult->statusMessage = "Error calculating checksum";
		return outResult->calculationStatus;
	}
	
	// Step 3: Compare
	if (!outResult->attributeExists) {
		outResult->isValid = false;
		outResult->statusMessage = "No checksum attribute found";
		_WriteBackStatus(filePath, "unverified");
		return B_OK;
	}

	if (outResult->attributedChecksum != outResult->calculatedChecksum) {
		outResult->isValid = false;
		outResult->statusMessage = "Checksum mismatch - file may be corrupted";
		_WriteBackStatus(filePath, "corrupted");
		return B_OK;
	}

	outResult->isValid = true;
	outResult->statusMessage = "Verified OK";
	_WriteBackStatus(filePath, "ok");
	return B_OK;
}


status_t
PackageVerifier::_ReadAttribute(const char* filePath,
	const char* attrName,
	BString& outValue)
{
	BFile file(filePath, B_READ_ONLY);
	status_t status = file.InitCheck();
	if (status != B_OK)
		return status;
	
	// Get attribute size first
	attr_info attrInfo;
	status = file.GetAttrInfo(attrName, &attrInfo);
	if (status != B_OK)
		return status;
	
	// Read attribute value
	char buffer[1024] = {0};
	ssize_t bytesRead = file.ReadAttr(attrName, B_STRING_TYPE, 0,
		buffer, sizeof(buffer) - 1);
	
	if (bytesRead > 0) {
		buffer[bytesRead] = '\0';
		outValue = buffer;
		return B_OK;
	}
	
	return B_ERROR;
}


status_t
PackageVerifier::_CalculateChecksum(const char* filePath,
	BString& outChecksum)
{
	BFile file(filePath, B_READ_ONLY);
	status_t status = file.InitCheck();
	if (status != B_OK)
		return status;

	// Use BCrypto for SHA256 calculation in streaming mode
	BCrypto crypto;
	uint8 digest[32];
	memset(digest, 0, sizeof(digest));

	// Calculate SHA256 using streaming API (efficient for large files)
	status = crypto.Digest(B_CRYPTO_SHA256, &file, digest);
	if (status != B_OK) {
		fprintf(stderr, "PackageVerifier: Failed to calculate SHA256: %s\n",
			strerror(status));
		return status;
	}

	// Convert binary digest to hex string
	char hexBuffer[65];
	_HPKGDigestToHex(digest, sizeof(digest), hexBuffer);
	outChecksum = hexBuffer;

	return B_OK;
}


void
PackageVerifier::_WriteBackStatus(const char* filePath, const char* status)
{
	BNode node(filePath);
	if (node.InitCheck() != B_OK)
		return;

	node_ref nref;
	if (node.GetNodeRef(&nref) == B_OK) {
		_HPKGEnsureIndex(nref.device, HPKG_ATTR_STATUS);
		_HPKGEnsureIndex(nref.device, HPKG_ATTR_VERIFIED);
	}

	size_t statusLen = strlen(status);
	node.WriteAttr(HPKG_ATTR_STATUS, B_STRING_TYPE, 0, status,
		statusLen + 1);

	time_t now = time(NULL);
	struct tm* timeinfo = gmtime(&now);
	if (timeinfo != NULL) {
		char tsBuffer[HPKG_TIMESTAMP_BUFFER_SIZE];
		strftime(tsBuffer, sizeof(tsBuffer), "%Y-%m-%dT%H:%M:%S UTC",
			timeinfo);
		node.WriteAttr(HPKG_ATTR_VERIFIED, B_STRING_TYPE, 0, tsBuffer,
			strlen(tsBuffer) + 1);
	}
}
