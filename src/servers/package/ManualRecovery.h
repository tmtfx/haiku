#ifndef _MANUAL_RECOVERY_H
#define _MANUAL_RECOVERY_H

#include "RecoveryStrategy.h"
#include <String.h>

// Manual user-guided recovery strategy
// For packages from unknown sources where automatic recovery isn't possible
class ManualRecovery : public RecoveryStrategy {
public:
	ManualRecovery(RecoveryLogger* logger = NULL,
		RecoveryNotifier* notifier = NULL);
	virtual ~ManualRecovery();
	
	// Can handle any non-repository package
	virtual bool CanRecover(const PackageCorruptionInfo& corruption);
	
	// Strategy name
	virtual const char* StrategyName() const { return "ManualRecovery"; }
	
	// Describe the recovery
	virtual BString DescribeRecovery(const PackageCorruptionInfo& corruption);
	
	// Perform manual recovery (shows UI for file selection)
	virtual status_t Recover(
		const PackageCorruptionInfo& corruption,
		RecoveryResult& outResult);
	
	// This strategy ALWAYS requires user interaction
	virtual bool RequiresUserInteraction() const { return true; }
	
	// Allow specifying replacement file programmatically
	// (for testing or headless recovery)
	status_t RecoverWithFile(
		const PackageCorruptionInfo& corruption,
		const char* replacementFilePath,
		RecoveryResult& outResult);
	
private:
	RecoveryLogger* fLogger;
	RecoveryNotifier* fNotifier;
	
	// Show file selection dialog to user
	// Returns selected path or NULL if cancelled
	char* _ShowFileSelectionDialog(
		const char* packageName);
	
	// Verify file is valid HPKG
	status_t _ValidateHPKGFile(
		const char* filePath);
	
	// Verify checksum matches expected
	status_t _VerifyChecksum(
		const char* filePath,
		const BString& expectedChecksum);
	
	// Copy replacement file to package location
	status_t _CopyReplacementFile(
		const char* sourcePath,
		const char* destinationPath);
	
	// Re-activate package through package daemon
	status_t _ReactivatePackage(
		const char* packagePath);
	
	// Get human-readable failure reason
	BString _GetFailureReason(
		const PackageCorruptionInfo& corruption);
};

#endif // _MANUAL_RECOVERY_H
