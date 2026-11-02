/*
 * Copyright 2026, Pirati del Frico. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Copilot CLI
 *		tmtfx
 *
 * Implementation of HPKG integrity checking utilities
 */


#include "HPKGIntegrityConstants.h"

#include <string.h>


namespace BPackageKit {

namespace BPrivate {


// ==============================================================================
// Convert Binary Digest to Hex String
// ==============================================================================
// Input:  32-byte binary SHA256 digest
// Output: 64-character hex string + null terminator
// Example: [0xAB, 0xCD, ...] → "abcd..."
//
void
_HPKGDigestToHex(const uint8* digest, size_t digestLen, char* outHex)
{
	if (digest == NULL || outHex == NULL)
		return;

	for (size_t i = 0; i < digestLen && i < HPKG_CHECKSUM_LENGTH_BYTES; i++) {
		uint8 byte = digest[i];
		outHex[i * 2] = _HPKGNibbleToHexChar((byte >> 4) & 0x0F);
		outHex[i * 2 + 1] = _HPKGNibbleToHexChar(byte & 0x0F);
	}
	
	// Null-terminate at proper position
	size_t hexLen = (digestLen < HPKG_CHECKSUM_LENGTH_BYTES) 
		? digestLen * 2 
		: HPKG_CHECKSUM_LENGTH_HEX;
	outHex[hexLen] = '\0';
}


// ==============================================================================
// Convert Hex String to Binary Digest
// ==============================================================================
// Input:  64-character hex string
// Output: 32-byte binary digest
// Returns: B_OK on success, B_BAD_VALUE if hex string is invalid
//
status_t
_HPKGHexToDigest(const char* hexStr, size_t hexLen, uint8* outDigest)
{
	if (hexStr == NULL || outDigest == NULL)
		return B_BAD_VALUE;

	// Must be exactly 64 hex characters for SHA256
	if (hexLen != HPKG_CHECKSUM_LENGTH_HEX)
		return B_BAD_VALUE;

	// Convert each pair of hex characters to a byte
	for (size_t i = 0; i < HPKG_CHECKSUM_LENGTH_BYTES; i++) {
		int highNibble = _HPKGHexCharToNibble(hexStr[i * 2]);
		int lowNibble = _HPKGHexCharToNibble(hexStr[i * 2 + 1]);

		// Check for invalid hex characters
		if (highNibble < 0 || lowNibble < 0)
			return B_BAD_VALUE;

		outDigest[i] = (highNibble << 4) | lowNibble;
	}

	return B_OK;
}


// ==============================================================================
// Validate SHA256 Hex String
// ==============================================================================
// Checks if a string is a valid 64-character hex representation
//
bool
_HPKGIsValidChecksumHex(const char* hexStr)
{
	if (hexStr == NULL)
		return false;

	// Check length
	if (strlen(hexStr) != HPKG_CHECKSUM_LENGTH_HEX)
		return false;

	// Check all characters are hex digits
	for (size_t i = 0; i < HPKG_CHECKSUM_LENGTH_HEX; i++) {
		char ch = hexStr[i];
		bool isHexDigit = (ch >= '0' && ch <= '9')
			|| (ch >= 'a' && ch <= 'f')
			|| (ch >= 'A' && ch <= 'F');
		if (!isHexDigit)
			return false;
	}

	return true;
}


}	// namespace BPrivate

}	// namespace BPackageKit
