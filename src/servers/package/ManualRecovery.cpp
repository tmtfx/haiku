#include "ManualRecovery.h"
#include "HPKGIntegrityConstants.h"

#include <File.h>
#include <SupportDefs.h>
#include <Errors.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// For BCrypto SHA256
#include <crypto/BCrypto.h>

using namespace BPackageKit::BPrivate;

ManualRecovery::ManualRecovery(
	RecoveryLogger* logger,
	RecoveryNotifier* notifier)
	: fLogger(logger), fNotifier(notifier)
{
}

ManualRecovery::~ManualRecovery()
{
}

bool
ManualRecovery::CanRecover(const PackageCorruptionInfo& corruption)
{
	// Can recover packages from unknown, network, local-backup, or
	// self-built sources - all of these need user intervention to locate
	// (or rebuild) a replacement file; only a genuine repository package
	// can be fetched automatically (RepositoryRecovery).
	return corruption.sourceType == RECOVERY_SOURCE_UNKNOWN ||
		corruption.sourceType == RECOVERY_SOURCE_NETWORK ||
		corruption.sourceType == RECOVERY_SOURCE_LOCAL_FILE ||
		corruption.sourceType == RECOVERY_SOURCE_BUILD;
}

BString
ManualRecovery::DescribeRecovery(const PackageCorruptionInfo& corruption)
{
	BString description;
	description.SetToFormat(
		"Manual recovery: Locate and select the original %s file",
		corruption.packageName.String());
	return description;
}

BString
ManualRecovery::_GetFailureReason(
	const PackageCorruptionInfo& corruption)
{
	BString reason;
	
	switch (corruption.sourceType) {
		case RECOVERY_SOURCE_UNKNOWN:
			reason = "Package from unknown source cannot be automatically recovered";
			break;
		case RECOVERY_SOURCE_NETWORK:
			reason = "Package from third-party network source is not available";
			break;
		case RECOVERY_SOURCE_LOCAL_FILE:
			reason = "Local backup location no longer accessible";
			break;
		default:
			reason = "Automatic recovery not available for this package";
			break;
	}
	
	return reason;
}

char*
ManualRecovery::_ShowFileSelectionDialog(
	const char* packageName)
{
	// TODO: This should show a real file selection dialog
	// For now, return NULL to indicate user cancelled
	// In production, this would:
	// 1. Open a file browser window using BFilePanel
	// 2. Pre-filter to *.hpkg files
	// 3. Wait for user selection
	// 4. Return selected path or NULL if cancelled
	
	fprintf(stderr, "INFO: Would show file selection dialog for %s\n", packageName);
	return NULL;
}

status_t
ManualRecovery::_ValidateHPKGFile(
	const char* filePath)
{
	if (!filePath)
		return B_BAD_VALUE;
	
	// Check file exists
	BFile file(filePath, B_READ_ONLY);
	status_t status = file.InitCheck();
	if (status != B_OK)
		return status;
	
	// TODO: Validate HPKG magic number and header
	// HPKG files start with specific magic bytes
	// For now, just check it's readable and has reasonable size
	
	off_t fileSize = 0;
	file.GetSize(&fileSize);
	
	if (fileSize < 1024) {  // Minimum HPKG size
		return B_BAD_DATA;
	}
	
	return B_OK;
}

status_t
ManualRecovery::_VerifyChecksum(
	const char* filePath,
	const BString& expectedChecksum)
{
	if (!filePath)
		return B_BAD_VALUE;

	BFile file(filePath, B_READ_ONLY);
	status_t status = file.InitCheck();
	if (status != B_OK)
		return status;

	BCrypto crypto;
	uint8 digest[HPKG_CHECKSUM_LENGTH_BYTES];
	status = crypto.Digest(B_CRYPTO_SHA256, &file, digest);
	if (status != B_OK)
		return status;

	char hexBuffer[HPKG_CHECKSUM_BUFFER_SIZE];
	_HPKGDigestToHex(digest, sizeof(digest), hexBuffer);

	if (expectedChecksum != hexBuffer) {
		fprintf(stderr, "ManualRecovery: checksum mismatch for %s "
			"(expected %s, got %s)\n", filePath,
			expectedChecksum.String(), hexBuffer);
		return B_BAD_DATA;
	}

	return B_OK;
}

status_t
ManualRecovery::Recover(
	const PackageCorruptionInfo& corruption,
	RecoveryResult& outResult)
{
	// Show notification to user
	if (fNotifier) {
		fNotifier->NotifyManualRecoveryNeeded(
			corruption.packageName.String(),
			_GetFailureReason(corruption).String());
	}
	
	// Initialize result
	outResult.status = RECOVERY_STATUS_MANUAL_REQUIRED;
	outResult.recoveryAttemptTime = time(NULL);
	outResult.wasAutomatic = false;
	outResult.statusMessage = "Waiting for user to locate package file";
	
	if (fLogger) {
		fLogger->LogRecoveryStart(corruption, StrategyName());
	}
	
	// Show file selection dialog
	char* selectedFile = _ShowFileSelectionDialog(
		corruption.packageName.String());
	
	if (!selectedFile) {
		// User cancelled
		outResult.status = RECOVERY_STATUS_SKIPPED;
		outResult.statusMessage = "User cancelled recovery";
		outResult.recoveryCompleteTime = time(NULL);
		
		if (fLogger)
			fLogger->LogRecoveryComplete(outResult);
		
		return B_CANCELED;
	}
	
	// Use the selected file for recovery
	status_t status = RecoverWithFile(corruption, selectedFile, outResult);
	free(selectedFile);
	
	return status;
}

status_t
ManualRecovery::RecoverWithFile(
	const PackageCorruptionInfo& corruption,
	const char* replacementFilePath,
	RecoveryResult& outResult)
{
	if (!replacementFilePath)
		return B_BAD_VALUE;
	
	outResult.status = RECOVERY_STATUS_IN_PROGRESS;
	outResult.recoveryAttemptTime = time(NULL);
	outResult.wasAutomatic = false;
	
	if (fLogger) {
		fLogger->LogRecoveryStart(corruption, StrategyName());
	}
	
	// Step 1: Validate replacement file is valid HPKG
	status_t status = _ValidateHPKGFile(replacementFilePath);
	if (status != B_OK) {
		outResult.status = RECOVERY_STATUS_FAILED;
		outResult.statusMessage = "Selected file is not a valid HPKG package";
		outResult.errorCode = status;
		
		if (fLogger)
			fLogger->LogRecoveryComplete(outResult);
		
		return status;
	}
	
	// Step 2: Verify checksum matches expected
	status = _VerifyChecksum(replacementFilePath, corruption.expectedChecksum);
	if (status != B_OK) {
		outResult.status = RECOVERY_STATUS_FAILED;
		outResult.statusMessage = "Selected file checksum does not match";
		outResult.errorCode = status;
		outResult.errorDetail = "Make sure you selected the correct package version";
		
		if (fLogger)
			fLogger->LogRecoveryComplete(outResult);
		
		return status;
	}
	
	// Step 3: Overwrite the corrupted package's content in place. This
	// is a deliberate choice over any install/activate path: replacing
	// file content under an unchanged directory entry never touches
	// packagefs's live mount, so there is no dependency check to get
	// wrong either way (unlike `pkgman uninstall`, which would propose
	// removing every *healthy* package depending on this one too). The
	// fix takes effect on the next reboot.
	status = HPKGOverwritePackageContent(
		corruption.packagePath.String(), replacementFilePath);
	if (status != B_OK) {
		outResult.status = RECOVERY_STATUS_FAILED;
		outResult.statusMessage = "Failed to write the repaired content";
		outResult.errorCode = status;

		if (fLogger)
			fLogger->LogRecoveryComplete(outResult);

		return status;
	}

	// Success!
	outResult.status = RECOVERY_STATUS_SUCCESS;
	outResult.statusMessage = "Content repaired from the selected file - "
		"a REBOOT is required for the fix to take effect";
	outResult.recoveredFilePath = corruption.packagePath;
	outResult.recoveryCompleteTime = time(NULL);

	if (fLogger)
		fLogger->LogRecoveryComplete(outResult);

	if (fNotifier) {
		fNotifier->NotifyAutoRecoverySuccess(
			corruption.packageName.String(),
			"Content repaired from user-selected file; reboot required");
	}

	return B_OK;
}
