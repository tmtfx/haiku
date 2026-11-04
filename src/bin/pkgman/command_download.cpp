/*
 * Copyright 2026, Pirati del Frico. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */


#include <getopt.h>
#include <new>
#include <stdio.h>
#include <stdlib.h>

#include <package/solver/SolverPackage.h>
#include <package/solver/SolverPackageSpecifier.h>
#include <package/solver/SolverPackageSpecifierList.h>
#include <package/solver/SolverRepository.h>

#include <Entry.h>
#include <Path.h>

#include "Command.h"
#include "pkgman.h"
#include "PackageManager.h"


// TODO: internationalization!


using namespace BPackageKit;
using namespace BPackageKit::BPrivate;
using namespace BPackageKit::BManager::BPrivate;


static const char* const kShortUsage =
	"  %command% <package> [-o <path>]\n"
	"    Downloads a package from its repository without installing it.\n";

static const char* const kLongUsage =
	"Usage: %program% %command% <package> [-o <path>]\n"
	"Resolves <package> against the configured repositories - the same way\n"
	"\"install\" does - and downloads the matching .hpkg file, verifying its\n"
	"checksum. Unlike \"install\", this never touches the installed state:\n"
	"it does not activate anything, and does not care whether <package> is\n"
	"already installed. On success, prints the downloaded file's path and\n"
	"its repository-recorded SHA256 checksum.\n"
	"\n"
	"Options:\n"
	"  --debug <level>\n"
	"    Print debug output. <level> should be between 0 (no debug output,\n"
	"    the default) and 10 (most debug output).\n"
	"  -o, --output <path>\n"
	"    Where to save the downloaded file. Defaults to the package's own\n"
	"    file name in the current directory.\n"
	"  -R, --no-refresh\n"
	"    Do not refresh the repository caches.\n"
	"\n";


DEFINE_COMMAND(DownloadCommand, "download", kShortUsage, kLongUsage,
	COMMAND_CATEGORY_PACKAGES)


int
DownloadCommand::Execute(int argc, const char* const* argv)
{
	const char* outputPath = NULL;
	bool refresh = true;

	while (true) {
		static struct option sLongOptions[] = {
			{ "debug", required_argument, 0, OPTION_DEBUG },
			{ "help", no_argument, 0, 'h' },
			{ "output", required_argument, 0, 'o' },
			{ "no-refresh", no_argument, 0, 'R' },
			{ 0, 0, 0, 0 }
		};

		opterr = 0; // don't print errors
		int c = getopt_long(argc, (char**)argv, "ho:R", sLongOptions, NULL);
		if (c == -1)
			break;

		if (fCommonOptions.HandleOption(c))
			continue;

		switch (c) {
			case 'h':
				PrintUsageAndExit(false);
				break;

			case 'o':
				outputPath = optarg;
				break;

			case 'R':
				refresh = false;
				break;

			default:
				PrintUsageAndExit(true);
				break;
		}
	}

	// Exactly one package argument.
	if (argc != optind + 1)
		PrintUsageAndExit(true);

	const char* packageName = argv[optind];

	// Set up the solver against installed + remote repositories, exactly
	// like "install" does - we just never call Install()/apply anything,
	// so none of the dependency/activation machinery is involved.
	PackageManager packageManager(B_PACKAGE_INSTALLATION_LOCATION_SYSTEM,
		false);
	packageManager.SetDebugLevel(fCommonOptions.DebugLevel());
	packageManager.Init(BPackageManager::B_ADD_INSTALLED_REPOSITORIES
		| BPackageManager::B_ADD_REMOTE_REPOSITORIES
		| (refresh ? BPackageManager::B_REFRESH_REPOSITORIES : 0));

	BSolverPackageSpecifierList specifiers;
	if (!specifiers.AppendSpecifier(BString(packageName)))
		throw std::bad_alloc();

	BObjectList<BSolverPackage> foundPackages;
	const BSolverPackageSpecifier* unmatched;
	status_t error = packageManager.Solver()->FindPackages(specifiers, 0,
		foundPackages, &unmatched);
	if (error != B_OK || foundPackages.IsEmpty()) {
		fprintf(stderr, "Error: package \"%s\" not found in any configured "
			"repository\n", packageName);
		return 1;
	}

	// FindPackages() without B_FIND_INSTALLED_ONLY can return an
	// already-installed match too - we specifically want something with
	// a remote repository to download from.
	BSolverPackage* package = NULL;
	BPackageManager::RemoteRepository* remoteRepository = NULL;
	for (int32 i = 0; BSolverPackage* candidate = foundPackages.ItemAt(i);
			i++) {
		remoteRepository = dynamic_cast<BPackageManager::RemoteRepository*>(
			candidate->Repository());
		if (remoteRepository != NULL) {
			package = candidate;
			break;
		}
	}

	if (package == NULL) {
		fprintf(stderr, "Error: \"%s\" is not available from any remote "
			"repository (only an already-installed copy was found)\n",
			packageName);
		return 1;
	}

	BString fileName(package->Info().FileName());
	if (fileName.IsEmpty()) {
		fprintf(stderr, "Error: package has no file name\n");
		return 1;
	}

	BPath targetPath;
	if (outputPath != NULL)
		targetPath.SetTo(outputPath);
	else
		targetPath.SetTo(fileName.String());

	BEntry targetEntry(targetPath.Path());

	BString url = remoteRepository->Config().PackagesURL();
	url << '/' << fileName;

	const BString& checksum = package->Info().Checksum();

	fprintf(stdout, "Downloading %s from %s ...\n",
		package->VersionedName().String(), url.String());

	error = packageManager.DownloadPackage(url, targetEntry, checksum);
	if (error != B_OK) {
		fprintf(stderr, "Error: download failed: %s\n", strerror(error));
		return 1;
	}

	BPath resultPath;
	targetEntry.GetPath(&resultPath);

	fprintf(stdout, "OK\n");
	fprintf(stdout, "path: %s\n", resultPath.Path());
	fprintf(stdout, "sha256: %s\n", checksum.String());

	return 0;
}
