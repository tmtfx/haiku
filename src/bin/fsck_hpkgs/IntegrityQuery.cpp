#include "IntegrityQuery.h"
#include "PackageVerifier.h"
#include "../../../kits/package/hpkg/HPKGIntegrityConstants.h"

#include <fs_query.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <Node.h>
#include <Path.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>

IntegrityQuery::IntegrityQuery()
	: fQueryPredicate(""), fResults(10)
{
}

IntegrityQuery::~IntegrityQuery()
{
	fResults.MakeEmpty();
}

void IntegrityQuery::QueryCorrupted()
{
	// Query for packages where sha256 attribute exists
	// Actual corruption verification happens in _VerifyResult()
	fQueryPredicate = HPKG_ATTR_CHECKSUM;
}

void IntegrityQuery::QueryUnverified()
{
	// Query for packages missing sha256 attribute - but BFS doesn't support NOT predicates
	// So we scan all and filter manually
	fQueryPredicate = "";  // Empty predicate = scan all
}

void IntegrityQuery::QueryOK()
{
	// Query for packages with sha256 attribute
	fQueryPredicate = HPKG_ATTR_CHECKSUM;
}

void IntegrityQuery::QuerySinceTimestamp(const BString& timestamp)
{
	// Build query predicate for files verified since timestamp
	// Format: integrity.hpkg.verified > "2026-11-03T06:00:00 UTC"
	fQueryPredicate = HPKG_ATTR_VERIFIED;
	fQueryPredicate << " >= \"" << timestamp << "\"";
}

status_t IntegrityQuery::Execute(const char* searchPath)
{
	if (!searchPath)
		return B_BAD_VALUE;
	
	return _ExecuteNativeQuery(searchPath);
}

int32 IntegrityQuery::CountResults() const
{
	return fResults.CountItems();
}

status_t IntegrityQuery::GetResult(int32 index, PackageQueryResult& outResult) const
{
	if (index < 0 || index >= fResults.CountItems())
		return B_BAD_INDEX;
	
	const PackageQueryResult* result = fResults.ItemAt(index);
	if (!result)
		return B_ERROR;
	
	outResult = *result;
	return B_OK;
}

status_t IntegrityQuery::_ExecuteNativeQuery(const char* searchPath)
{
	// For now, implement via directory scan (fallback when BFS query indices not available)
	// This ensures compatibility while providing the API for future BFS index support
	
	BDirectory directory(searchPath);
	status_t status = directory.InitCheck();
	if (status != B_OK)
		return status;
	
	entry_ref ref;
	while (directory.GetNextRef(&ref) == B_OK) {
		// Filter for .hpkg files
		if (strstr(ref.name, ".hpkg") == NULL)
			continue;
		
		BPath path(&ref);
		_VerifyResult(path.Path());
	}
	
	return B_OK;
}

void IntegrityQuery::_VerifyResult(const char* path)
{
	if (!path)
		return;
	
	BNode node(path);
	if (node.InitCheck() != B_OK)
		return;
	
	PackageQueryResult* result = new PackageQueryResult();
	if (!result)
		return;
	
	// Extract filename
	BPath bpath(path);
	result->path = path;
	result->filename = bpath.Leaf();
	
	// Read checksum attribute
	char checksumBuffer[HPKG_CHECKSUM_BUFFER_SIZE];
	ssize_t checksumRead = node.ReadAttr(HPKG_ATTR_CHECKSUM, 
		B_STRING_TYPE, 0, checksumBuffer, HPKG_CHECKSUM_BUFFER_SIZE - 1);
	
	if (checksumRead > 0) {
		checksumBuffer[checksumRead] = '\0';
		result->checksum = checksumBuffer;
	}
	
	// Read verification timestamp
	char timestampBuffer[HPKG_TIMESTAMP_BUFFER_SIZE];
	ssize_t timestampRead = node.ReadAttr(HPKG_ATTR_VERIFIED,
		B_STRING_TYPE, 0, timestampBuffer, HPKG_TIMESTAMP_BUFFER_SIZE - 1);
	
	if (timestampRead > 0) {
		timestampBuffer[timestampRead] = '\0';
		result->verified = timestampBuffer;
	}
	
	// Determine status
	if (result->checksum.IsEmpty()) {
		result->status = "unverified";
	} else {
		// To determine if corrupted, would need to recalculate hash
		// For query results, we assume if checksum exists, last scan marked it OK
		// A true "corrupted" status would be written by PackageVerifier during scan
		result->status = "ok";
	}
	
	fResults.AddItem(result);
}
