#include "RepositoryRecovery.h"
#include "HPKGIntegrityConstants.h"

#include <File.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// For BCrypto SHA256
#include <crypto/BCrypto.h>

using namespace BPackageKit::BPrivate;

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
		"Auto-recover from repository: download %s and overwrite its "
		"content in place (reboot required afterwards)",
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
RepositoryRecovery::_VerifyChecksum(
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
		fprintf(stderr, "RepositoryRecovery: checksum mismatch for %s "
			"(expected %s, got %s)\n", filePath,
			expectedChecksum.String(), hexBuffer);
		return B_BAD_DATA;
	}

	return B_OK;
}

status_t
RepositoryRecovery::Recover(
	const PackageCorruptionInfo& corruption,
	RecoveryResult& outResult)
{
	outResult.status = RECOVERY_STATUS_IN_PROGRESS;
	outResult.recoveryAttemptTime = time(NULL);
	outResult.wasAutomatic = true;

	if (fNotifier)
		fNotifier->NotifyAutoRecoveryStarted(corruption.packageName.String());
	if (fLogger)
		fLogger->LogRecoveryStart(corruption, StrategyName());

	BString packageName, packageVersion;
	status_t status = _ExtractPackageInfo(
		corruption.packagePath.String(), packageName, packageVersion);

	if (status != B_OK) {
		outResult.status = RECOVERY_STATUS_FAILED;
		outResult.statusMessage = "Failed to extract package info";
		outResult.errorCode = status;
		if (fLogger)
			fLogger->LogRecoveryComplete(outResult);
		return status;
	}

	BString stagedPath(corruption.packagePath);
	stagedPath << ".download";

	BString downloadedChecksum;
	status = HPKGPkgmanDownload(packageName.String(), stagedPath.String(),
		downloadedChecksum);
	if (status != B_OK) {
		outResult.status = RECOVERY_STATUS_FAILED;
		outResult.statusMessage = "pkgman download failed (repository "
			"unreachable, or the package is no longer available there)";
		outResult.errorCode = status;
		if (fLogger)
			fLogger->LogRecoveryComplete(outResult);
		if (fNotifier) {
			fNotifier->NotifyAutoRecoveryFailed(
				corruption.packageName.String(),
				"Could not download a replacement from the repository");
		}
		remove(stagedPath.String());
		return status;
	}

	// The repository may have moved on to a different version of this
	// package since it was installed - only an exact match against what
	// we originally recorded is safe to use for a repair.
	if (downloadedChecksum != corruption.expectedChecksum) {
		outResult.status = RECOVERY_STATUS_FAILED;
		outResult.statusMessage = "The repository has a different version "
			"than the one originally installed - cannot safely auto-repair";
		outResult.errorDetail.SetToFormat("expected %s, repository has %s",
			corruption.expectedChecksum.String(),
			downloadedChecksum.String());
		if (fLogger)
			fLogger->LogRecoveryComplete(outResult);
		if (fNotifier) {
			fNotifier->NotifyAutoRecoveryFailed(
				corruption.packageName.String(),
				"Repository version has changed since installation");
		}
		remove(stagedPath.String());
		return B_BAD_DATA;
	}

	// Independently verify the downloaded bytes too, rather than trusting
	// pkgman's own internal check (and the string match above) blindly.
	status = _VerifyChecksum(stagedPath.String(), corruption.expectedChecksum);
	if (status != B_OK) {
		outResult.status = RECOVERY_STATUS_FAILED;
		outResult.statusMessage = "Downloaded file failed independent "
			"checksum verification";
		outResult.errorCode = status;
		if (fLogger)
			fLogger->LogRecoveryComplete(outResult);
		if (fNotifier) {
			fNotifier->NotifyAutoRecoveryFailed(
				corruption.packageName.String(),
				"Downloaded file did not verify");
		}
		remove(stagedPath.String());
		return status;
	}

	status = HPKGOverwritePackageContent(
		corruption.packagePath.String(), stagedPath.String());
	remove(stagedPath.String());

	if (status != B_OK) {
		outResult.status = RECOVERY_STATUS_FAILED;
		outResult.statusMessage = "Failed to write the repaired content";
		outResult.errorCode = status;
		if (fLogger)
			fLogger->LogRecoveryComplete(outResult);
		if (fNotifier) {
			fNotifier->NotifyAutoRecoveryFailed(
				corruption.packageName.String(),
				"Failed to overwrite the corrupted file");
		}
		return status;
	}

	outResult.status = RECOVERY_STATUS_SUCCESS;
	outResult.statusMessage = "Content repaired from the repository - a "
		"REBOOT is required for the fix to take effect";
	outResult.recoveredFilePath = corruption.packagePath;
	outResult.recoveryCompleteTime = time(NULL);

	if (fLogger)
		fLogger->LogRecoveryComplete(outResult);

	if (fNotifier) {
		BString detail;
		detail.SetToFormat("Version: %s, Source: repository - reboot "
			"required", packageVersion.String());
		fNotifier->NotifyAutoRecoverySuccess(
			corruption.packageName.String(), detail.String());
	}

	return B_OK;
}
