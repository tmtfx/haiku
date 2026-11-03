/*
 * Copyright 2026, Pirati del Frico. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Copilot CLI
 *		tmtfx
 *
 * Purpose:
 *		Constants and utilities for HPKG data integrity checking via
 *		BFS extended attributes. Provides SHA256 checksum storage
 *		and verification support for package ecosystem stability.
 */

#ifndef _PACKAGE_HPKG_INTEGRITY_CONSTANTS_H_
#define _PACKAGE_HPKG_INTEGRITY_CONSTANTS_H_


#include <SupportDefs.h>
#include <String.h>


namespace BPackageKit {

namespace BPrivate {


// ==============================================================================
// Extended Attribute Names
// ==============================================================================
// BFS extended attributes follow the format: family.namespace.name
// Example: integrity.hpkg.sha256
// - family "integrity" = system integrity metadata
// - namespace "hpkg" = package-related attributes
// - name = specific attribute purpose

#define HPKG_ATTR_CHECKSUM	"integrity.hpkg.sha256"		// SHA256 hex string
#define HPKG_ATTR_VERIFIED	"integrity.hpkg.verified"	// Last verification timestamp
#define HPKG_ATTR_SOURCE	"integrity.hpkg.source"		// Package source (e.g., "repository")


// ==============================================================================
// Checksum Format Specifications
// ==============================================================================
// SHA256 produces 256 bits = 32 bytes
// Hex encoding: each byte → 2 hex characters
// Example: d4 → "d4", so 32 bytes → 64 hex characters

#define HPKG_CHECKSUM_ALGORITHM			"SHA256"
#define HPKG_CHECKSUM_LENGTH_BYTES		32		// SHA256 = 32 bytes
#define HPKG_CHECKSUM_LENGTH_HEX		64		// 32 bytes × 2 hex chars
#define HPKG_CHECKSUM_BUFFER_SIZE		65		// 64 chars + null terminator


// ==============================================================================
// Timestamp Format Specifications
// ==============================================================================
// Timestamps stored as ISO 8601 strings with UTC timezone
// Format: "2026-11-02T22:10:15 UTC" or similar
// Used in user.hpkg.verified attribute

#define HPKG_TIMESTAMP_BUFFER_SIZE		32		// ISO 8601 + null


// ==============================================================================
// Source Name Specifications
// ==============================================================================
// Source indicates where the package came from (e.g., "repository", "local")
// Useful for tracking package origin and recovery procedures

#define HPKG_SOURCE_REPOSITORY			"repository"
#define HPKG_SOURCE_LOCAL				"local"
#define HPKG_SOURCE_BUFFER_SIZE			32		// Space for source name + null


// ==============================================================================
// Attribute Type Information
// ==============================================================================
// BFS attribute type for integrity checking attributes
// All integrity attributes are stored as strings (B_STRING_TYPE)

#define HPKG_ATTRIBUTE_TYPE				B_STRING_TYPE


// ==============================================================================
// Utility Constants
// ==============================================================================

// Nibble (4-bit) to hex character conversion
// Used in binary-to-hex encoding functions
// nibble 0-9 → '0'-'9'
// nibble 10-15 → 'a'-'f' (lowercase)
#define HPKG_NIBBLE_TO_HEX_LOW(n)		('0' + (n))
#define HPKG_NIBBLE_TO_HEX_HIGH(n)		('a' + (n) - 10)


// ==============================================================================
// Helper Functions
// ==============================================================================

// Convert a byte to uppercase hex string (e.g., 0xAB → "ab")
// Returns the hex character for the given nibble (4-bit value)
inline char
_HPKGNibbleToHexChar(uint8 nibble)
{
	nibble &= 0x0F;
	return (nibble >= 10) ? ('a' + nibble - 10) : ('0' + nibble);
}


// Convert hex character to nibble value (e.g., 'a' → 10)
// Returns -1 if character is not a valid hex digit
inline int
_HPKGHexCharToNibble(char ch)
{
	if (ch >= '0' && ch <= '9')
		return ch - '0';
	if (ch >= 'a' && ch <= 'f')
		return 10 + ch - 'a';
	if (ch >= 'A' && ch <= 'F')
		return 10 + ch - 'A';
	return -1;
}


// Convert a byte value to a 2-character hex string
// Example: DigestByteToHex(0xAB) → "ab"
// buffer must have at least 3 bytes (2 hex chars + null terminator)
inline void
_HPKGDigestByteToHex(uint8 byte, char* buffer)
{
	buffer[0] = _HPKGNibbleToHexChar((byte >> 4) & 0x0F);
	buffer[1] = _HPKGNibbleToHexChar(byte & 0x0F);
	buffer[2] = '\0';
}


// Convert binary digest (32 bytes) to hex string (64 chars)
// Example: DigestToHex([0xAB, 0xCD, ...], 32, outHex)
// outHex must have at least HPKG_CHECKSUM_BUFFER_SIZE capacity
void _HPKGDigestToHex(const uint8* digest, size_t digestLen, char* outHex);


// Convert hex string (64 chars) back to binary digest (32 bytes)
// Example: HexToDigest("abcd...", 64, outDigest)
// Returns B_OK on success, B_BAD_VALUE if hex string is malformed
// outDigest must have capacity for at least 32 bytes
status_t _HPKGHexToDigest(const char* hexStr, size_t hexLen, uint8* outDigest);


// Validate if a string is a valid SHA256 hex representation
// Valid: 64 characters, all hex digits (0-9, a-f, A-F)
bool _HPKGIsValidChecksumHex(const char* hexStr);


}	// namespace BPrivate

}	// namespace BPackageKit


#endif	// _PACKAGE_HPKG_INTEGRITY_CONSTANTS_H_
