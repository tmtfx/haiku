#include "RepositoryRecovery.h"
#include "HPKGIntegrityConstants.h"

#include <File.h>
#include <Path.h>
#include <Directory.h>
#include <Entry.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// For BCrypto SHA256
#include <crypto/SHA256.h>

RepositoryRecovery::RepositoryRecovery(
	RecoveryLogger* logger,
	RecoveryNotifier* notifier)
	: fLogger(logger), fNotifier(notifier)
{
}

RepositoryRecovery::~RepositoryRecovery()
{
}

bool
RepositoryRecovery::CanRecover(const PackageCorruptionInfo& corruption)
{
	// Only handle repository-sourced packages
	return corruption.sourceType == RECOVERY_SOURCE_REPOSITORY;
}

BString
RepositoryRecovery::DescribeRecovery(const PackageCorruptionInfo& corruption)
{
	BString description;
	description.SetToFormat(
		"Auto-recover from repository: Re-download %s and re-activate",
		corruption.packageName.String());
	return description;
}

status_t
RepositoryRecovery::_ExtractPackageInfo(
	const char* packagePath,
	BString& outName,
	BString& outVersion)
{
	if (!packagePath)
		return B_BAD_VALUE;
	
	// Extract filename from path
	const char* filename = strrchr(packagePath, '/');
	if (filename)
		filename++;
	else
		filename = packagePath;
	
	// Remove .hpkg extension
	BString cleanName(filename);
	if (cleanName.EndsWith(".hpkg"))
		cleanName.Truncate(cleanName.Length() - 5);
	
	// Try to split on last dash to separate name from version
	// Format: name-version.hpkg
	int32 lastDash = cleanName.FindLast('-');
	if (lastDash > 0) {
		outName = cleanName;
		outName.Truncate(lastDash);
		outVersion = cleanName;
		outVersion.Remove(0, lastDash + 1);
	} else {
		outName = cleanName;
		outVersion = "unknown";
	}
	
	return B_OK;
}

status_t
RepositoryRecovery::_ExtractVersionFromFilename(
	const char* filename,
	BString& outVersion)
{
	if (!filename)
		return B_BAD_VALUE;
	
	// Find .hpkg extension
	const char* ext = strrchr(filename, '.');
	if (!ext || strcmp(ext, ".hpkg") != 0)
		return B_BAD_VALUE;
	
	// Find last dash before extension
	BString temp(filename);
	temp.Truncate(ext - filename);
	
	int32 lastDash = temp.FindLast('-');
	if (lastDash > 0) {
		outVersion = temp;
		outVersion.Remove(0, lastDash + 1);
		return B_OK;
	}
	
	return B_ERROR;
}

status_t
RepositoryRecovery::_FindPackageInRepository(
	const char* packageName,
	const char* packageVersion,
	BString& outRepositoryPath)
{
	// TODO: This is a stub for the real integration with package manager
	// In production, this would:
	// 1. Connect to pkgman daemon via BMessenger
	// 2. Query available repositories
	// 3. Search for package name in repository index
	// 4. Return download URL/path
	
	// For now, we'll log and return a placeholder
	fprintf(stderr, "INFO: Would query repository for %s (version %s)\n",
		packageName, packageVersion);
	
	// Placeholder: construct a repository URL
	// Real implementation would get this from pkgman
	outRepositoryPath.SetToFormat("repository://%s-%s.hpkg",
		packageName, packageVersion);
	
	return B_OK;
}

status_t
RepositoryRecovery::_DownloadFromRepository(
	const char* repositoryPath,
	const BString& expectedChecksum,
	BString& outDownloadedPath)
{
	// TODO: This is a stub for actual download implementation
	// In production, this would:
	// 1. Parse repository path/URL
	// 2. Use curl or similar to download
	// 3. Verify checksum during download
	// 4. Save to temporary location
	// 5. Return path to temporary file
	
	fprintf(stderr, "INFO: Would download from repository: %s\n", repositoryPath);
	fprintf(stderr, "INFO: Expected checksum: %s\n", expectedChecksum.String());
	
	// For now, return an error (real implementation would download)
	return B_NOT_IMPLEMENTED;
}

status_t
RepositoryRecovery::_VerifyChecksum(
	const char* filePath,
	const BString& expectedChecksum)
{
	if (!filePath)
		return B_BAD_VALUE;
	
	// TODO: Integrate with BCrypto SHA256 from IntegrityQuery or create helper
	// This would verify the file's SHA256 matches expected
	
	fprintf(stderr, "INFO: Would verify checksum of %s\n", filePath);
	fprintf(stderr, "INFO: Expected: %s\n", expectedChecksum.String());
	
	// Stub implementation
	return B_NOT_IMPLEMENTED;
}

status_t
RepositoryRecovery::_DeactivateCorruptedPackage(
	const char* packagePath,
	BString& outBackupPath)
{
	if (!packagePath)
		return B_BAD_VALUE;
	
	// TODO: This is critical safety - must use package daemon
	// In production:
	// 1. Query package manager for dependents
	// 2. Check if any critical packages depend on this
	// 3. If safe, deactivate through package daemon
	// 4. Back up original file to recovery location
	
	fprintf(stderr, "INFO: Would deactivate package: %s\n", packagePath);
	
	// Create backup path
	BString backupPath(packagePath);
	backupPath.Append(".corrupted");
	outBackupPath = backupPath;
	
	// Stub - real implementation would use package daemon
	return B_NOT_IMPLEMENTED;
}

status_t
RepositoryRecovery::_ReactivatePackage(
	const char* packagePath)
{
	if (!packagePath)
		return B_BAD_VALUE;
	
	// TODO: Use package daemon to activate package
	// This is the critical point where the recovered package
	// gets activated into the system
	
	fprintf(stderr, "INFO: Would re-activate package: %s\n", packagePath);
	
	// Stub - real implementation would use package daemon IPC
	return B_NOT_IMPLEMENTED;
}

status_t
RepositoryRecovery::Recover(
	const PackageCorruptionInfo& corruption,
	RecoveryResult& outResult)
{
	// Initialize result
	outResult.status = RECOVERY_STATUS_IN_PROGRESS;
	outResult.recoveryAttemptTime = time(NULL);
	outResult.wasAutomatic = true;
	
	if (fNotifier) {
		fNotifier->NotifyAutoRecoveryStarted(corruption.packageName.String());
	}
	
	if (fLogger) {
		fLogger->LogRecoveryStart(corruption, StrategyName());
	}
	
	// Step 1: Extract package name and version
	BString packageName, packageVersion;
	status_t status = _ExtractPackageInfo(
		corruption.packagePath.String(),
		packageName, packageVersion);
	
	if (status != B_OK) {
		outResult.status = RECOVERY_STATUS_FAILED;
		outResult.statusMessage = "Failed to extract package info";
		outResult.errorCode = status;
		if (fLogger)
			fLogger->LogRecoveryComplete(outResult);
		return status;
	}
	
	// Step 2: Find package in repository
	BString repositoryPath;
	status = _FindPackageInRepository(
		packageName.String(),
		packageVersion.String(),
		repositoryPath);
	
	if (status != B_OK) {
		outResult.status = RECOVERY_STATUS_FAILED;
		outResult.statusMessage = "Package not found in repository";
		outResult.errorCode = status;
		if (fLogger)
			fLogger->LogRecoveryComplete(outResult);
		if (fNotifier) {
			fNotifier->NotifyAutoRecoveryFailed(
				corruption.packageName.String(),
				"Package not found in any repository");
		}
		return status;
	}
	
	// Step 3: Download from repository
	BString downloadedPath;
	status = _DownloadFromRepository(
		repositoryPath.String(),
		corruption.expectedChecksum,
		downloadedPath);
	
	if (status != B_OK) {
		outResult.status = RECOVERY_STATUS_FAILED;
		outResult.statusMessage = "Failed to download from repository";
		outResult.errorCode = status;
		if (fLogger)
			fLogger->LogRecoveryComplete(outResult);
		if (fNotifier) {
			fNotifier->NotifyAutoRecoveryFailed(
				corruption.packageName.String(),
				"Repository download failed");
		}
		return status;
	}
	
	// Step 4: Verify checksum
	status = _VerifyChecksum(
		downloadedPath.String(),
		corruption.expectedChecksum);
	
	if (status != B_OK) {
		outResult.status = RECOVERY_STATUS_FAILED;
		outResult.statusMessage = "Downloaded file checksum mismatch";
		outResult.errorCode = status;
		if (fLogger)
			fLogger->LogRecoveryComplete(outResult);
		if (fNotifier) {
			fNotifier->NotifyAutoRecoveryFailed(
				corruption.packageName.String(),
				"Checksum verification failed");
		}
		// Clean up downloaded file
		remove(downloadedPath.String());
		return status;
	}
	
	// Step 5: Deactivate corrupted package
	BString backupPath;
	status = _DeactivateCorruptedPackage(
		corruption.packagePath.String(),
		backupPath);
	
	if (status != B_OK) {
		outResult.status = RECOVERY_STATUS_FAILED;
		outResult.statusMessage = "Failed to deactivate corrupted package";
		outResult.errorCode = status;
		if (fLogger)
			fLogger->LogRecoveryComplete(outResult);
		remove(downloadedPath.String());
		return status;
	}
	
	// Step 6: Move recovered package to original location
	status = rename(downloadedPath.String(), corruption.packagePath.String());
	if (status != 0) {
		outResult.status = RECOVERY_STATUS_FAILED;
		outResult.statusMessage = "Failed to install recovered package";
		outResult.errorCode = errno;
		if (fLogger)
			fLogger->LogRecoveryComplete(outResult);
		remove(downloadedPath.String());
		return B_IO_ERROR;
	}
	
	// Step 7: Re-activate package
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
		BString detail;
		detail.SetToFormat("Version: %s, Source: repository",
			packageVersion.String());
		fNotifier->NotifyAutoRecoverySuccess(
			corruption.packageName.String(),
			detail.String());
	}
	
	return B_OK;
}
