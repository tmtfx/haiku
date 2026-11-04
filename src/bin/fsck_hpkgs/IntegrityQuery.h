#ifndef INTEGRITY_QUERY_H
#define INTEGRITY_QUERY_H

#include <String.h>
#include <ObjectList.h>
#include <fs_query.h>

// Query result structure for package integrity status
struct PackageQueryResult {
	BString path;
	BString filename;
	BString status;        // "ok", "corrupted", "unverified"
	BString checksum;      // hex SHA256 or empty
	BString verified;      // ISO 8601 timestamp or empty
	
	PackageQueryResult()
		: status("unverified"), checksum(""), verified("") {}
};

// IntegrityQuery: Build and execute BFS queries for integrity attributes
class IntegrityQuery {
public:
	IntegrityQuery();
	~IntegrityQuery();
	
	// Query predicates
	void QueryCorrupted();       // integrity.hpkg.sha256 exists but mismatches
	void QueryUnverified();      // integrity.hpkg.sha256 missing
	void QueryOK();              // integrity.hpkg.sha256 exists and valid
	void QuerySinceTimestamp(const BString& timestamp);
	
	// Execute query on specified directory
	status_t Execute(const char* searchPath);
	
	// Results access
	int32 CountResults() const;
	status_t GetResult(int32 index, PackageQueryResult& outResult) const;
	
	// Query info
	const char* GetQueryPredicateString() const { return fQueryPredicate.String(); }
	
private:
	BString fQueryPredicate;
	BObjectList<PackageQueryResult> fResults;
	
	// Helper: Build BFS query predicate string
	void _BuildPredicate();
	
	// Helper: Execute fs_open_query / fs_read_dir
	status_t _ExecuteNativeQuery(const char* searchPath);
	
	// Helper: Verify a result by reading attributes
	void _VerifyResult(const char* path);
};

#endif // INTEGRITY_QUERY_H
