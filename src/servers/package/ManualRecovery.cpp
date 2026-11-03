#include "ManualRecovery.h"
#include "HPKGIntegrityConstants.h"

#include <File.h>
#include <Path.h>
#include <Directory.h>
#include <Entry.h>
#include <SupportDefs.h>
#include <Errors.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <errno.h>

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
	// Can recover packages from unknown or network sources
	// These need user intervention to locate the file
	return corruption.sourceType == RECOVERY_SOURCE_UNKNOWN ||
		corruption.sourceType == RECOVERY_SOURCE_NETWORK ||
		corruption.sourceType == RECOVERY_SOURCE_LOCAL_FILE;
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
ManualRecovery::_DeactivateCorruptedPackage(
	const char* packagePath,
	BString& outBackupPath)
{
	return HPKGQuarantinePackage(packagePath, outBackupPath);
}

status_t
ManualRecovery::_CopyReplacementFile(
	const char* sourcePath,
	const char* destinationPath)
{
	if (!sourcePath || !destinationPath)
		return B_BAD_VALUE;
	
	// Open source file
	BFile source(sourcePath, B_READ_ONLY);
	if (source.InitCheck() != B_OK)
		return source.InitCheck();
	
	// Create destination file
	BFile dest(destinationPath, B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (dest.InitCheck() != B_OK)
		return dest.InitCheck();
	
	// Copy in chunks
	char buffer[64 * 1024];  // 64KB chunks
	ssize_t bytesRead;
	
	while ((bytesRead = source.Read(buffer, sizeof(buffer))) > 0) {
		ssize_t bytesWritten = dest.Write(buffer, bytesRead);
		if (bytesWritten != bytesRead)
			return B_IO_ERROR;
	}
	
	return B_OK;
}

status_t
ManualRecovery::_ReactivatePackage(
	const char* packagePath)
{
	return HPKGConfirmPackageInPlace(packagePath);
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
	
	// Step 3: Stage the verified replacement next to the target, under a
	// temporary name. Never overwrite the live package file in place:
	// packagefs's node monitor reacts to the directory entry being
	// created/removed/moved, not to a file's content changing underneath
	// an unchanged entry, so an in-place rewrite would silently leave the
	// corrupted package mounted.
	BString stagedPath(corruption.packagePath);
	stagedPath << ".recovered";

	status = _CopyReplacementFile(replacementFilePath, stagedPath.String());
	if (status != B_OK) {
		outResult.status = RECOVERY_STATUS_FAILED;
		outResult.statusMessage = "Failed to stage replacement file";
		outResult.errorCode = status;

		if (fLogger)
			fLogger->LogRecoveryComplete(outResult);

		remove(stagedPath.String());
		return status;
	}

	// Step 4: Deactivate the corrupted package - moving it out of the
	// packages directory is what actually unmounts it.
	BString backupPath;
	status = _DeactivateCorruptedPackage(
		corruption.packagePath.String(), backupPath);
	if (status != B_OK) {
		outResult.status = RECOVERY_STATUS_FAILED;
		outResult.statusMessage = "Failed to deactivate corrupted package";
		outResult.errorCode = status;

		if (fLogger)
			fLogger->LogRecoveryComplete(outResult);

		remove(stagedPath.String());
		return status;
	}

	// Step 5: Move the verified replacement into the original path -
	// this is what actually re-activates it.
	if (rename(stagedPath.String(), corruption.packagePath.String()) != 0) {
		outResult.status = RECOVERY_STATUS_FAILED;
		outResult.statusMessage = "Failed to install recovered package";
		outResult.errorCode = errno;

		if (fLogger)
			fLogger->LogRecoveryComplete(outResult);

		remove(stagedPath.String());
		return B_IO_ERROR;
	}

	// Step 6: Confirm re-activation
	status = _ReactivatePackage(corruption.packagePath.String());
	if (status != B_OK) {
		outResult.status = RECOVERY_STATUS_FAILED;
		outResult.statusMessage = "Failed to re-activate recovered package";
		outResult.errorCode = status;

		if (fLogger)
			fLogger->LogRecoveryComplete(outResult);

		return status;
	}

	// Success!
	outResult.status = RECOVERY_STATUS_SUCCESS;
	outResult.statusMessage = "Package recovered and re-activated successfully";
	outResult.recoveredFilePath = corruption.packagePath;
	outResult.recoveryCompleteTime = time(NULL);
	
	if (fLogger)
		fLogger->LogRecoveryComplete(outResult);
	
	if (fNotifier) {
		fNotifier->NotifyAutoRecoverySuccess(
			corruption.packageName.String(),
			"User-selected file activated");
	}
	
	return B_OK;
}
