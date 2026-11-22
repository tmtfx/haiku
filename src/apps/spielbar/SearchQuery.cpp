/*
 * SpielBar v3 - query asincrona per il mondo Cerca
 *
 * Generato da tools/split_app.py a partire da App.cpp (logica invariata).
 */

#include <Entry.h>
#include <Message.h>
#include <Query.h>
#include <String.h>
#include <SupportDefs.h>
#include <Volume.h>
#include <VolumeRoster.h>
#include <ctype.h>

#include "SearchQuery.h"
#include "SpielBarDefs.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SpielBar"


BString
BuildCaseInsensitivePattern(const BString& text, bool contains)
{
	BString pattern;
	if (contains)
		pattern << "*";

	for (int i = 0; i < text.Length(); i++) {
		unsigned char c = (unsigned char)text[i];
		if (c == '*' || c == '?' || c == '[' || c == '\\') {
			pattern << '\\' << (char)c;
		} else if (c < 128 && isalpha(c)) {
			pattern << "[" << (char)tolower(c) << (char)toupper(c) << "]";
		} else {
			pattern << (char)c;
		}
	}

	if (contains)
		pattern << "*";

	return pattern;
}

int32
SearchWorker(void* data)
{
	SearchParams* p = (SearchParams*)data;
	const int32 kBatchSize = 20;
	const int32 kResultLimit = 1000;

	BString pattern = BuildCaseInsensitivePattern(p->text, true);
	BString predicate;
	predicate.SetToFormat("name=%s", pattern.String());

	int32 total = 0;
	int32 inBatch = 0;
	bool truncated = false;
	bool cancelled = false;

	BMessage batch(kMsgSearchResults);
	batch.AddInt32("gen", (int32)p->myGeneration);

	BVolumeRoster roster;
	BVolume volume;
	while (!cancelled && !truncated && roster.GetNextVolume(&volume) == B_OK) {
		if (!volume.IsPersistent() || !volume.KnowsQuery())
			continue;

		BQuery query;
		query.SetVolume(&volume);
		query.SetPredicate(predicate.String());
		if (query.Fetch() != B_OK)
			continue;

		entry_ref ref;
		while (query.GetNextRef(&ref) == B_OK) {
			if (p->generation->load() != p->myGeneration) {
				cancelled = true;
				break;
			}
			batch.AddRef("refs", &ref);
			inBatch++;
			total++;

			if (inBatch >= kBatchSize) {
				if (p->target.SendMessage(&batch) != B_OK) {
					cancelled = true;
					break;
				}
				batch.MakeEmpty();
				batch.what = kMsgSearchResults;
				batch.AddInt32("gen", (int32)p->myGeneration);
				inBatch = 0;
			}
			if (total >= kResultLimit) {
				truncated = true;
				break;
			}
		}
	}

	if (!cancelled) {
		if (inBatch > 0)
			p->target.SendMessage(&batch);
		BMessage done(kMsgSearchDone);
		done.AddInt32("gen", (int32)p->myGeneration);
		done.AddBool("truncated", truncated);
		p->target.SendMessage(&done);
	}

	delete p;
	return 0;
}
