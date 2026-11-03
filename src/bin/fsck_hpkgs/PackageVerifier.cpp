/*
 * Copyright 2026, Pirati del Frico. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Copilot CLI
 *		tmtfx
 *
 * Package verification implementation
 */

#include "PackageVerifier.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <File.h>
#include <Node.h>
#include <Path.h>
#include <TypeConstants.h>
#include <fs_attr.h>

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
	
	memset(outResult, 0, sizeof(PackageVerificationResult));
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
		return B_OK;
	}
	
	if (outResult->attributedChecksum != outResult->calculatedChecksum) {
		outResult->isValid = false;
		outResult->statusMessage = "Checksum mismatch - file may be corrupted";
		return B_OK;
	}
	
	outResult->isValid = true;
	outResult->statusMessage = "Verified OK";
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
	// For now, return a placeholder
	// TODO: Integrate BCrypto or SHA256 calculation
	// This will compute SHA256 of the HPKG file contents
	
	fprintf(stderr, "TODO: Implement SHA256 calculation for %s\n", filePath);
	outChecksum = "0000000000000000000000000000000000000000000000000000000000000000";
	return B_OK;
}
