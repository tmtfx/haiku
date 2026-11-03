#ifndef _REPOSITORY_RECOVERY_H
#define _REPOSITORY_RECOVERY_H

#include "RecoveryStrategy.h"
#include <String.h>

// Repository-based automatic recovery strategy
// Handles packages installed from the repository
class RepositoryRecovery : public RecoveryStrategy {
public:
	RepositoryRecovery(RecoveryLogger* logger = NULL,
		RecoveryNotifier* notifier = NULL);
	virtual ~RepositoryRecovery();
	
	// Check if we can recover (only for repository packages)
	virtual bool CanRecover(const PackageCorruptionInfo& corruption);
	
	// Strategy name
	virtual const char* StrategyName() const { return "RepositoryRecovery"; }
	
	// Describe the recovery
	virtual BString DescribeRecovery(const PackageCorruptionInfo& corruption);
	
	// Perform automatic recovery
	virtual status_t Recover(
		const PackageCorruptionInfo& corruption,
		RecoveryResult& outResult);
	
	// No user interaction for repository recovery
	virtual bool RequiresUserInteraction() const { return false; }
	
private:
	RecoveryLogger* fLogger;
	RecoveryNotifier* fNotifier;
	
	// Extract package name from path (e.g., "/boot/system/packages/curl-7.85.0-2.hpkg" -> "curl")
	status_t _ExtractPackageInfo(
		const char* packagePath,
		BString& outName,
		BString& outVersion);
	
	// Query package manager for download source
	status_t _FindPackageInRepository(
		const char* packageName,
		const char* packageVersion,
		BString& outRepositoryPath);
	
	// Download package from repository with checksum verification
	status_t _DownloadFromRepository(
		const char* repositoryPath,
		const BString& expectedChecksum,
		BString& outDownloadedPath);
	
	// Verify downloaded file checksum
	status_t _VerifyChecksum(
		const char* filePath,
		const BString& expectedChecksum);
	
	// Deactivate corrupted package safely
	status_t _DeactivateCorruptedPackage(
		const char* packagePath,
		BString& outBackupPath);
	
	// Re-activate recovered package through package daemon
	status_t _ReactivatePackage(
		const char* packagePath);
	
	// Parse version string from filename
	status_t _ExtractVersionFromFilename(
		const char* filename,
		BString& outVersion);
};

#endif // _REPOSITORY_RECOVERY_H
