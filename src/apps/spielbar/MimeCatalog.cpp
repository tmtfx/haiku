/*
 * SpielBar v3 - percorsi noti, classificazione MIME, origine dei pacchetti
 *
 * Generato da tools/split_app.py a partire da App.cpp (logica invariata).
 */

#include <AppFileInfo.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <LocaleRoster.h>
#include <MimeType.h>
#include <Node.h>
#include <Path.h>
#include <StorageDefs.h>
#include <String.h>
#include <SupportDefs.h>
#include <string.h>

#include "MimeCatalog.h"
#include "SpielBarTypes.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SpielBar"


const KnownPaths&
Paths()
{
	static KnownPaths paths;
	return paths;
}

bool
PathStartsWith(const char* path, const BString& prefix)
{
	if (prefix.Length() == 0)
		return false;
	size_t n = (size_t)prefix.Length();
	if (strncmp(path, prefix.String(), n) != 0)
		return false;
	return path[n] == '\0' || path[n] == '/' || prefix[n - 1] == '/';
}

bool
IsUnderPreferences(const entry_ref& ref)
{
	BPath path(&ref);
	if (path.InitCheck() != B_OK)
		return false;
	return PathStartsWith(path.Path(), Paths().sysPrefs)
		|| PathStartsWith(path.Path(), Paths().userPrefs);
}

uint32
ClassifyEntry(const entry_ref& ref, bool prefletHint, BString* signatureOut)
{
	BEntry entry(&ref, true);
	if (entry.InitCheck() != B_OK)
		return kKindFile;
	if (entry.IsDirectory())
		return kKindFolder;

	entry_ref target;
	if (entry.GetRef(&target) != B_OK)
		target = ref;

	BFile file(&target, B_READ_ONLY);
	if (file.InitCheck() == B_OK) {
		BAppFileInfo info(&file);
		char sig[B_MIME_TYPE_LENGTH];
		if (info.GetSignature(sig) == B_OK && sig[0] != '\0') {
			if (signatureOut != NULL)
				*signatureOut = sig;
			bool preflet = prefletHint || IsUnderPreferences(ref)
				|| IsUnderPreferences(target);
			return preflet ? kKindPreflet : kKindApp;
		}
	}
	return kKindFile;
}

bool
IsHaikuBasePackage(const BString& name)
{
	return name.FindFirst("haiku") == 0 || name.FindFirst("_hrev") >= 0;
}

bool
FindOriginPackage(const entry_ref& ref, entry_ref* out)
{
	BNode node(&ref);
	BString file;
	if (node.InitCheck() != B_OK
		|| node.ReadAttrString("SYS:PACKAGE_FILE", &file) != B_OK
		|| file.Length() == 0)
		return false;

	const directory_which dirs[2]
		= { B_SYSTEM_PACKAGES_DIRECTORY, B_USER_PACKAGES_DIRECTORY };
	for (int i = 0; i < 2; i++) {
		BPath path;
		if (find_directory(dirs[i], &path) != B_OK
			|| path.Append(file.String()) != B_OK)
			continue;
		BEntry entry(path.Path());
		if (entry.Exists() && entry.GetRef(out) == B_OK)
			return true;
	}
	return false;
}

PackageOrigin
ReadPackageOrigin(const entry_ref& ref)
{
	entry_ref target = ref;
	BEntry entry(&ref, true);
	if (entry.InitCheck() == B_OK)
		entry.GetRef(&target);

	BNode node(&target);
	if (node.InitCheck() == B_OK) {
		BString value;
		// (verificare par. 9.5) esistenza e contenuto di SYS:PACKAGE
		if (node.ReadAttrString("SYS:PACKAGE", &value) == B_OK
			&& value.Length() > 0) {
			return IsHaikuBasePackage(value) ? kSystem : kPackaged;
		}
	}

	// Fallback senza attributo, approssimazione dichiarata.
	BPath path(&target);
	if (path.InitCheck() == B_OK) {
		if (PathStartsWith(path.Path(), Paths().sysNonPackaged)
			|| PathStartsWith(path.Path(), Paths().userNonPackaged))
			return kNonPackaged;
		if (PathStartsWith(path.Path(), Paths().system))
			return kSystem;
	}
	return kNonPackaged;
}

BString
DisplayNameFor(const char* signature, const entry_ref& ref)
{
	static int preferred = -1;
	if (preferred < 0)
		preferred = BLocaleRoster::Default()->IsFilesystemTranslationPreferred()
			? 1 : 0;

	if (preferred == 1) {
		BString localizedName;
		if (BLocaleRoster::Default()->GetLocalizedFileName(localizedName, ref)
				== B_OK && localizedName.Length() > 0)
			return localizedName;
	}
	BString label = ref.name;
	if (signature && signature[0]) {
		BMimeType mt(signature);
		if (mt.IsInstalled()) {
			char shortDesc[B_MIME_TYPE_LENGTH];
			if (mt.GetShortDescription(shortDesc) == B_OK
				&& shortDesc[0] != '\0')
				label = shortDesc;
		}
	}
	return label;
}
