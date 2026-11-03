/*
 * Copyright 2026, Pirati del Frico. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Copilot CLI
 *		tmtfx
 *
 * Purpose:
 *		Write HPKG integrity checksums to BFS extended attributes
 *		during package activation. Provides non-blocking attribute
 *		persistence for data integrity verification.
 */

#ifndef HPKG_INTEGRITY_WRITER_H
#define HPKG_INTEGRITY_WRITER_H


#include <Entry.h>
#include <String.h>
#include <SupportDefs.h>


/*
 * HPKGIntegrityWriter
 *
 * Provides utilities for writing and managing HPKG integrity attributes.
 * All operations are non-blocking and log errors rather than throwing.
 *
 * Attributes written:
 *   integrity.hpkg.sha256  - SHA256 checksum (64-char hex)
 *   integrity.hpkg.verified - Verification timestamp (optional)
 */

class HPKGIntegrityWriter {
public:
	/*
	 * WriteChecksum()
	 *
	 * Write SHA256 checksum to package file's extended attributes.
	 * Non-blocking: logs errors but does not fail the transaction.
	 *
	 * Params:
	 *   entry       - entry_ref of the .hpkg file
	 *   checksum    - SHA256 hex string (64 characters)
	 *
	 * Returns:
	 *   B_OK        - Checksum written successfully
	 *   B_NO_MEMORY - Memory allocation failed
	 *   B_IO_ERROR  - Attribute write failed (non-fatal)
	 *   B_BAD_VALUE - Invalid checksum format
	 */
	static status_t WriteChecksum(const entry_ref& entry,
		const BString& checksum);


	/*
	 * ReadChecksum()
	 *
	 * Read SHA256 checksum from package file's extended attributes.
	 *
	 * Params:
	 *   entry       - entry_ref of the .hpkg file
	 *   outChecksum - Output string for the hex checksum
	 *
	 * Returns:
	 *   B_OK            - Checksum read successfully
	 *   B_ENTRY_NOT_FOUND - Attribute does not exist (not an error)
	 *   B_IO_ERROR      - Attribute read failed
	 *   B_BAD_VALUE     - Checksum attribute is corrupted
	 */
	static status_t ReadChecksum(const entry_ref& entry,
		BString& outChecksum);


	/*
	 * UpdateVerificationTimestamp()
	 *
	 * Update the verification timestamp attribute after integrity check.
	 * This helps track when each package was last verified.
	 *
	 * Params:
	 *   entry       - entry_ref of the .hpkg file
	 *   timestamp   - ISO 8601 timestamp string (optional, uses current time if empty)
	 *
	 * Returns:
	 *   B_OK        - Timestamp updated successfully
	 *   B_IO_ERROR  - Attribute write failed (non-fatal)
	 */
	static status_t UpdateVerificationTimestamp(const entry_ref& entry,
		const BString& timestamp = BString());


private:
	/*
	 * Helper: Write generic attribute
	 */
	static status_t _WriteAttribute(const entry_ref& entry,
		const char* attributeName, const char* value);

	/*
	 * Helper: Read generic attribute
	 */
	static status_t _ReadAttribute(const entry_ref& entry,
		const char* attributeName, BString& outValue);

	/*
	 * Helper: Generate ISO 8601 timestamp
	 */
	static BString _GetCurrentTimestamp();

	/*
	 * Helper: Validate checksum format (must be 64 hex chars)
	 */
	static bool _IsValidChecksum(const BString& checksum);
};


#endif	// HPKG_INTEGRITY_WRITER_H
