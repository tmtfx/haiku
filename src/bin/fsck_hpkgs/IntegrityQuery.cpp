#include "IntegrityQuery.h"
#include "PackageVerifier.h"
#include "../../../kits/package/hpkg/HPKGIntegrityConstants.h"

#include <fs_query.h>
#include <fs_info.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <Node.h>
#include <Path.h>
#include <Query.h>
#include <Volume.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>

using namespace BPackageKit::BPrivate;

IntegrityQuery::IntegrityQuery()
	: fQueryPredicate(""), fResults(10)
{
}

IntegrityQuery::~IntegrityQuery()
{
	fResults.MakeEmpty();
}

// These predicates match against integrity.hpkg.status, which
// PackageVerifier writes back on every scan (see PackageVerifier.cpp).
// A package that was never scanned has no such attribute at all, so it
// won't match any of the three - that's correct: "query" reflects the
// last scan, not a fresh ground truth (that's what "scan" is for).

void IntegrityQuery::QueryCorrupted()
{
	fQueryPredicate = HPKG_ATTR_STATUS;
	fQueryPredicate << "==\"corrupted\"";
}

void IntegrityQuery::QueryUnverified()
{
	fQueryPredicate = HPKG_ATTR_STATUS;
	fQueryPredicate << "==\"unverified\"";
}

void IntegrityQuery::QueryOK()
{
	fQueryPredicate = HPKG_ATTR_STATUS;
	fQueryPredicate << "==\"ok\"";
}

void IntegrityQuery::QuerySinceTimestamp(const BString& timestamp)
{
	// Build query predicate for files verified since timestamp
	// Format: integrity.hpkg.verified >= "2026-11-03T06:00:00 UTC"
	BString clause = HPKG_ATTR_VERIFIED;
	clause << " >= \"" << timestamp << "\"";

	// Combine with a status filter set earlier, if any, instead of
	// silently overwriting it.
	if (fQueryPredicate.IsEmpty())
		fQueryPredicate = clause;
	else
		fQueryPredicate << " && " << clause;
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
	// Real BFS query first. This works correctly whether or not an index
	// exists on the attribute (an index just makes it faster - see
	// _HPKGEnsureIndex); it can only fail outright for a syntactically
	// empty predicate (QueryUnverified() with nothing set) or a volume
	// that doesn't support queries at all (not BFS).
	if (!fQueryPredicate.IsEmpty()) {
		dev_t device = dev_for_path(searchPath);
		if (device >= 0) {
			BVolume volume(device);
			BQuery query;
			if (query.SetVolume(&volume) == B_OK
				&& query.SetPredicate(fQueryPredicate.String()) == B_OK
				&& query.Fetch() == B_OK) {
				entry_ref ref;
				while (query.GetNextRef(&ref) == B_OK) {
					if (strstr(ref.name, ".hpkg") == NULL)
						continue;

					BPath path(&ref);
					_VerifyResult(path.Path());
				}
				return B_OK;
			}
		}
	}

	// Fallback: directory scan. Used for an empty predicate (list
	// everything) or if the volume/query setup above didn't work out.
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
	
	// Prefer the status PackageVerifier wrote back during the last scan
	// (ok/corrupted/unverified - see PackageVerifier::_WriteBackStatus).
	// Only fall back to inferring from checksum presence for a package
	// that has never been scanned at all.
	char statusBuffer[32];
	ssize_t statusRead = node.ReadAttr(HPKG_ATTR_STATUS,
		B_STRING_TYPE, 0, statusBuffer, sizeof(statusBuffer) - 1);

	if (statusRead > 0) {
		statusBuffer[statusRead] = '\0';
		result->status = statusBuffer;
	} else if (result->checksum.IsEmpty()) {
		result->status = "unverified";
	} else {
		result->status = "ok";
	}

	fResults.AddItem(result);
}
