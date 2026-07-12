/*
 * Copyright 2012, Michael Lotz, mmlr@mlotz.ch. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */


#include "KeyStoreServer.h"

#include "AppAccessRequestWindow.h"
#include "KeyRequestWindow.h"
#include "MasterPasswordRequestWindow.h"
#include "Keyring.h"

#include <KeyStoreDefs.h>

#include <Directory.h>
#include <Entry.h>
#include <FindDirectory.h>
#include <Path.h>
#include <Roster.h>
#include <String.h>

#include <new>
#include <string.h>
#include <stdio.h>

#include <crypto/BCrypto.h>
#include <crypto/BCryptoDefs.h>

#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/err.h>
#include <openssl/x509.h>

#include <fs_attr.h>


using namespace BPrivate;


static const char* kMasterKeyringName = "Master";
static const char* kKeyringKeysIdentifier = "Keyrings";

static const uint32 kKeyStoreFormatVersion = 1;

static const uint32 kFlagGetKey						= 0x0001;
static const uint32 kFlagEnumerateKeys				= 0x0002;
static const uint32 kFlagAddKey						= 0x0004;
static const uint32 kFlagRemoveKey					= 0x0008;
static const uint32 kFlagAddKeyring					= 0x0010;
static const uint32 kFlagRemoveKeyring				= 0x0020;
static const uint32 kFlagEnumerateKeyrings			= 0x0040;
static const uint32 kFlagSetUnlockKey				= 0x0080;
static const uint32 kFlagRemoveUnlockKey			= 0x0100;
static const uint32 kFlagAddKeyringsToMaster		= 0x0200;
static const uint32 kFlagRemoveKeyringsFromMaster	= 0x0400;
static const uint32 kFlagEnumerateMasterKeyrings	= 0x0800;
static const uint32 kFlagQueryLockState				= 0x1000;
static const uint32 kFlagLockKeyring				= 0x2000;
static const uint32 kFlagEnumerateApplications		= 0x4000;
static const uint32 kFlagRemoveApplications			= 0x8000;

static const uint32 kDefaultAppFlags = kFlagGetKey | kFlagEnumerateKeys
	| kFlagAddKey | kFlagRemoveKey | kFlagAddKeyring | kFlagRemoveKeyring
	| kFlagEnumerateKeyrings | kFlagSetUnlockKey | kFlagRemoveUnlockKey
	| kFlagAddKeyringsToMaster | kFlagRemoveKeyringsFromMaster
	| kFlagEnumerateMasterKeyrings | kFlagQueryLockState | kFlagLockKeyring
	| kFlagEnumerateApplications | kFlagRemoveApplications;


// TODO RIMUOVERE FINITO IL DEBUG
#include <iomanip>
#include <sstream>

// Helper interno per stampare i buffer in esadecimale nei log
static std::string _BufToHex(const uint8_t* buf, size_t len) {
    std::ostringstream ss;
    for (size_t i = 0; i < len; ++i)
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)buf[i];
    return ss.str();
}
// ********************************

KeyStoreServer::KeyStoreServer()
	:
	BApplication(kKeyStoreServerSignature),
	fMasterKeyring(NULL),
	fKeyrings(20),
	fHasSessionPassword(false)
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK)
		return;

	BDirectory settingsDir(path.Path());
	path.Append("system");
	if (!settingsDir.Contains(path.Path()))
		settingsDir.CreateDirectory(path.Path(), NULL);

	settingsDir.SetTo(path.Path());
	path.Append("keystore");
	if (!settingsDir.Contains(path.Path()))
		settingsDir.CreateDirectory(path.Path(), NULL);

	settingsDir.SetTo(path.Path());
	path.Append("keystore_database");

	fKeyStoreFile.SetTo(path.Path(), B_READ_WRITE
		| (settingsDir.Contains(path.Path()) ? 0 : B_CREATE_FILE));

	_ReadKeyStoreDatabase();

	if (fMasterKeyring == NULL) {
		fMasterKeyring = new(std::nothrow) Keyring(kMasterKeyringName);
		fKeyrings.BinaryInsert(fMasterKeyring, &Keyring::Compare);
	}
}


KeyStoreServer::~KeyStoreServer()
{
}


void
KeyStoreServer::MessageReceived(BMessage* message)
{
	BMessage reply;
	status_t result = B_UNSUPPORTED;
	app_info callingAppInfo;

	uint32 accessFlags = _AccessFlagsFor(message->what);
	if (accessFlags == 0)
		message->what = 0;

	if (message->what != 0) {
		result = _ResolveCallingApp(*message, callingAppInfo);
		if (result != B_OK)
			message->what = 0;
	}

	// Resolve the keyring for the relevant messages.
	Keyring* keyring = NULL;
	switch (message->what) {
		case KEY_STORE_GET_KEY:
		case KEY_STORE_GET_NEXT_KEY:
		case KEY_STORE_ADD_KEY:
		case KEY_STORE_REMOVE_KEY:
		case KEY_STORE_IS_KEYRING_UNLOCKED:
		case KEY_STORE_LOCK_KEYRING:
		case KEY_STORE_SET_UNLOCK_KEY:
		case KEY_STORE_REMOVE_UNLOCK_KEY:
		case KEY_STORE_ADD_KEYRING_TO_MASTER:
		case KEY_STORE_REMOVE_KEYRING_FROM_MASTER:
		case KEY_STORE_GET_NEXT_APPLICATION:
		case KEY_STORE_REMOVE_APPLICATION:
		case KEY_STORE_GET_ENCRYPTED_KEY:
		case KEY_STORE_ADD_ENCRYPTED_KEY:
		{
			BString keyringName;
			if (message->FindString("keyring", &keyringName) != B_OK)
				keyringName = "";

			keyring = _FindKeyring(keyringName);
			if (keyring == NULL) {
				result = B_BAD_VALUE;
				message->what = 0;
					// So that we don't do anything in the second switch.
				break;
			}

			switch (message->what) {
				case KEY_STORE_GET_KEY:
				case KEY_STORE_GET_NEXT_KEY:
				case KEY_STORE_ADD_KEY:
				case KEY_STORE_REMOVE_KEY:
				case KEY_STORE_SET_UNLOCK_KEY:
				case KEY_STORE_REMOVE_UNLOCK_KEY:
				case KEY_STORE_ADD_KEYRING_TO_MASTER:
				case KEY_STORE_GET_NEXT_APPLICATION:
				case KEY_STORE_REMOVE_APPLICATION:
				case KEY_STORE_GET_ENCRYPTED_KEY:
				case KEY_STORE_ADD_ENCRYPTED_KEY:
				{
					// These need keyring access to do anything.
					while (!keyring->IsUnlocked()) {
						status_t unlockResult = _UnlockKeyring(*keyring);
						if (unlockResult != B_OK) {
							result = unlockResult;
							message->what = 0;
							break;
						}
					}

					status_t validateResult = _ValidateAppAccess(*keyring,
						callingAppInfo, accessFlags);
					if (validateResult != B_OK) {
						result = validateResult;
						message->what = 0;
						break;
					}

					break;
				}
			}

			break;
		}
	}

	switch (message->what) {
		case KEY_STORE_GET_KEY:
		{
			BString identifier;
			if (message->FindString("identifier", &identifier) != B_OK) {
				result = B_BAD_VALUE;
				break;
			}

			bool secondaryIdentifierOptional;
			if (message->FindBool("secondaryIdentifierOptional",
					&secondaryIdentifierOptional) != B_OK) {
				secondaryIdentifierOptional = false;
			}

			BString secondaryIdentifier;
			if (message->FindString("secondaryIdentifier",
					&secondaryIdentifier) != B_OK) {
				secondaryIdentifier = "";
				secondaryIdentifierOptional = true;
			}

			BMessage keyMessage;
			result = keyring->FindKey(identifier, secondaryIdentifier,
				secondaryIdentifierOptional, &keyMessage);
			if (result == B_OK)
				reply.AddMessage("key", &keyMessage);

			break;
		}

		case KEY_STORE_GET_NEXT_KEY:
		{
			BKeyType type;
			BKeyPurpose purpose;
			uint32 cookie;
			if (message->FindUInt32("type", (uint32*)&type) != B_OK
				|| message->FindUInt32("purpose", (uint32*)&purpose) != B_OK
				|| message->FindUInt32("cookie", &cookie) != B_OK) {
				result = B_BAD_VALUE;
				break;
			}

			BMessage keyMessage;
			result = keyring->FindKey(type, purpose, cookie, keyMessage);
			if (result == B_OK) {
				cookie++;
				reply.AddUInt32("cookie", cookie);
				reply.AddMessage("key", &keyMessage);
			}

			break;
		}

		case KEY_STORE_ADD_KEY:
		{
			BMessage keyMessage;
			BString identifier;
			if (message->FindMessage("key", &keyMessage) != B_OK
				|| keyMessage.FindString("identifier", &identifier) != B_OK) {
				result = B_BAD_VALUE;
				break;
			}

			BString secondaryIdentifier;
			if (keyMessage.FindString("secondaryIdentifier",
					&secondaryIdentifier) != B_OK) {
				secondaryIdentifier = "";
			}

			result = keyring->AddKey(identifier, secondaryIdentifier, keyMessage);
			if (result == B_OK)
				_WriteKeyStoreDatabase();

			break;
		}

		case KEY_STORE_REMOVE_KEY:
		{
			BMessage keyMessage;
			BString identifier;
			if (message->FindMessage("key", &keyMessage) != B_OK
				|| keyMessage.FindString("identifier", &identifier) != B_OK) {
				result = B_BAD_VALUE;
				break;
			}

			result = keyring->RemoveKey(identifier, keyMessage);
			if (result == B_OK)
				_WriteKeyStoreDatabase();

			break;
		}

		case KEY_STORE_ADD_KEYRING:
		{
			BMessage keyMessage;
			BString keyring;
			if (message->FindString("keyring", &keyring) != B_OK) {
				result = B_BAD_VALUE;
				break;
			}

			result = _AddKeyring(keyring);
			if (result == B_OK)
				_WriteKeyStoreDatabase();

			break;
		}

		case KEY_STORE_REMOVE_KEYRING:
		{
			BString keyringName;
			if (message->FindString("keyring", &keyringName) != B_OK)
				keyringName = "";

			result = _RemoveKeyring(keyringName);
			if (result == B_OK)
				_WriteKeyStoreDatabase();

			break;
		}

		case KEY_STORE_GET_NEXT_KEYRING:
		{
			uint32 cookie;
			if (message->FindUInt32("cookie", &cookie) != B_OK) {
				result = B_BAD_VALUE;
				break;
			}

			keyring = fKeyrings.ItemAt(cookie);
			if (keyring == NULL) {
				result = B_ENTRY_NOT_FOUND;
				break;
			}

			cookie++;
			reply.AddUInt32("cookie", cookie);
			reply.AddString("keyring", keyring->Name());
			result = B_OK;
			break;
		}

		case KEY_STORE_IS_KEYRING_UNLOCKED:
		{
			reply.AddBool("unlocked", keyring->IsUnlocked());
			result = B_OK;
			break;
		}

		case KEY_STORE_LOCK_KEYRING:
		{
			keyring->Lock();
			result = B_OK;
			break;
		}

		case KEY_STORE_SET_UNLOCK_KEY:
		{
			BMessage keyMessage;
			if (message->FindMessage("key", &keyMessage) != B_OK) {
				result = B_BAD_VALUE;
				break;
			}

			result = keyring->SetUnlockKey(keyMessage);
			if (result == B_OK)
				_WriteKeyStoreDatabase();

			// TODO: Update the key in the master if this keyring was added.
			break;
		}

		case KEY_STORE_REMOVE_UNLOCK_KEY:
		{
			result = keyring->RemoveUnlockKey();
			if (result == B_OK)
				_WriteKeyStoreDatabase();

			break;
		}

		case KEY_STORE_ADD_KEYRING_TO_MASTER:
		case KEY_STORE_REMOVE_KEYRING_FROM_MASTER:
		{
			// We also need access to the master keyring.
			while (!fMasterKeyring->IsUnlocked()) {
				status_t unlockResult = _UnlockKeyring(*fMasterKeyring);
				if (unlockResult != B_OK) {
					result = unlockResult;
					message->what = 0;
					break;
				}
			}

			if (message->what == 0)
				break;

			BString secondaryIdentifier = keyring->Name();
			BMessage keyMessage = keyring->UnlockKey();
			keyMessage.RemoveName("identifier");
			keyMessage.AddString("identifier", kKeyringKeysIdentifier);
			keyMessage.RemoveName("secondaryIdentifier");
			keyMessage.AddString("secondaryIdentifier", secondaryIdentifier);

			switch (message->what) {
				case KEY_STORE_ADD_KEYRING_TO_MASTER:
					result = fMasterKeyring->AddKey(kKeyringKeysIdentifier,
						secondaryIdentifier, keyMessage);
					break;

				case KEY_STORE_REMOVE_KEYRING_FROM_MASTER:
					result = fMasterKeyring->RemoveKey(kKeyringKeysIdentifier,
						keyMessage);
					break;
			}

			if (result == B_OK)
				_WriteKeyStoreDatabase();

			break;
		}

		case KEY_STORE_GET_NEXT_APPLICATION:
		{
			uint32 cookie;
			if (message->FindUInt32("cookie", &cookie) != B_OK) {
				result = B_BAD_VALUE;
				break;
			}

			BString signature;
			BString path;
			result = keyring->GetNextApplication(cookie, signature, path);
			if (result != B_OK)
				break;

			reply.AddUInt32("cookie", cookie);
			reply.AddString("signature", signature);
			reply.AddString("path", path);
			result = B_OK;
			break;
		}

		case KEY_STORE_REMOVE_APPLICATION:
		{
			const char* signature = NULL;
			const char* path = NULL;

			if (message->FindString("signature", &signature) != B_OK) {
				result = B_BAD_VALUE;
				break;
			}

			if (message->FindString("path", &path) != B_OK)
				path = NULL;

			result = keyring->RemoveApplication(signature, path);
			if (result == B_OK)
				_WriteKeyStoreDatabase();

			break;
		}

		case KEY_STORE_GET_ENCRYPTED_KEY:
		{
			// Ensure we have the session password before decrypting.
			result = _GetOrAskSessionPassword();
			if (result != B_OK)
				break;

			BString identifier;
			if (message->FindString("identifier", &identifier) != B_OK) {
				result = B_BAD_VALUE;
				break;
			}

			BString secondaryIdentifier;
			if (message->FindString("secondaryIdentifier",
					&secondaryIdentifier) != B_OK) {
				secondaryIdentifier = "";
			}

			BMessage keyMessage;
			result = keyring->FindKey(identifier, secondaryIdentifier, true,
				&keyMessage);
			if (result != B_OK)
				break;

			// Decrypt in-place before returning to caller.
			result = _DecryptKeyData(keyMessage);
			if (result == B_OK) {
				fprintf(stderr,"decifratura andata a buon fine, rispondo al mittente\n");
				reply.AddMessage("key", &keyMessage);
			} else {
				fprintf(stderr, "decifratura fallita!!!\n");
			}

			break;
		}

		case KEY_STORE_ADD_ENCRYPTED_KEY:
		{
			// Ensure we have the session password before encrypting.
			result = _GetOrAskSessionPassword();
			if (result != B_OK)
				break;

			BMessage keyMessage;
			BString identifier;
			if (message->FindMessage("key", &keyMessage) != B_OK
					|| keyMessage.FindString("identifier", &identifier) != B_OK) {
				result = B_BAD_VALUE;
				break;
			}

			// Encrypt the key data with the session password.
			result = _EncryptKeyData(keyMessage);
			if (result != B_OK) {
				fprintf(stderr,"cifratura della chiave fallita\n");
				break;
			}

			BString secondaryIdentifier;
			if (keyMessage.FindString("secondaryIdentifier",
					&secondaryIdentifier) != B_OK) {
				secondaryIdentifier = "";
			}

			result = keyring->AddKey(identifier, secondaryIdentifier, keyMessage);
			if (result == B_OK)
				_WriteKeyStoreDatabase();

			break;
		}

		case 0:
		{
			// Just the error case from above.
			break;
		}

		default:
		{
			printf("unknown message received: %" B_PRIu32 " \"%.4s\"\n",
				message->what, (const char*)&message->what);
			break;
		}
	}

	if (message->IsSourceWaiting()) {
		if (result == B_OK)
			reply.what = KEY_STORE_SUCCESS;
		else {
			reply.what = KEY_STORE_RESULT;
			reply.AddInt32("result", result);
		}

		message->SendReply(&reply);
	}
}


status_t
KeyStoreServer::_ReadKeyStoreDatabase()
{
	BMessage keystore;
	status_t result = keystore.Unflatten(&fKeyStoreFile);
	if (result != B_OK) {
		printf("failed to read keystore database\n");
		_WriteKeyStoreDatabase();
			// Reinitializes the database.
		return result;
	}

	int32 index = 0;
	BMessage keyringData;
	while (keystore.FindMessage("keyrings", index++, &keyringData) == B_OK) {
		Keyring* keyring = new(std::nothrow) Keyring();
		if (keyring == NULL) {
			printf("no memory for allocating keyring\n");
			break;
		}

		status_t result = keyring->ReadFromMessage(keyringData);
		if (result != B_OK) {
			printf("failed to read keyring from data\n");
			delete keyring;
			continue;
		}

		if (strcmp(keyring->Name(), kMasterKeyringName) == 0)
			fMasterKeyring = keyring;

		fKeyrings.BinaryInsert(keyring, &Keyring::Compare);
	}

	return B_OK;
}


status_t
KeyStoreServer::_WriteKeyStoreDatabase()
{
	BMessage keystore;
	keystore.AddUInt32("format", kKeyStoreFormatVersion);

	for (int32 i = 0; i < fKeyrings.CountItems(); i++) {
		Keyring* keyring = fKeyrings.ItemAt(i);
		if (keyring == NULL)
			continue;

		BMessage keyringData;
		status_t result = keyring->WriteToMessage(keyringData);
		if (result != B_OK)
			return result;

		keystore.AddMessage("keyrings", &keyringData);
	}

	fKeyStoreFile.SetSize(0);
	fKeyStoreFile.Seek(0, SEEK_SET);
	return keystore.Flatten(&fKeyStoreFile);
}


uint32
KeyStoreServer::_AccessFlagsFor(uint32 command) const
{
	switch (command) {
		case KEY_STORE_GET_KEY:
		case KEY_STORE_GET_ENCRYPTED_KEY:
			return kFlagGetKey;
		case KEY_STORE_GET_NEXT_KEY:
			return kFlagEnumerateKeys;
		case KEY_STORE_ADD_KEY:
		case KEY_STORE_ADD_ENCRYPTED_KEY:
			return kFlagAddKey;
		case KEY_STORE_REMOVE_KEY:
			return kFlagRemoveKey;
		case KEY_STORE_ADD_KEYRING:
			return kFlagAddKeyring;
		case KEY_STORE_REMOVE_KEYRING:
			return kFlagRemoveKeyring;
		case KEY_STORE_GET_NEXT_KEYRING:
			return kFlagEnumerateKeyrings;
		case KEY_STORE_SET_UNLOCK_KEY:
			return kFlagSetUnlockKey;
		case KEY_STORE_REMOVE_UNLOCK_KEY:
			return kFlagRemoveUnlockKey;
		case KEY_STORE_ADD_KEYRING_TO_MASTER:
			return kFlagAddKeyringsToMaster;
		case KEY_STORE_REMOVE_KEYRING_FROM_MASTER:
			return kFlagRemoveKeyringsFromMaster;
		case KEY_STORE_GET_NEXT_MASTER_KEYRING:
			return kFlagEnumerateMasterKeyrings;
		case KEY_STORE_IS_KEYRING_UNLOCKED:
			return kFlagQueryLockState;
		case KEY_STORE_LOCK_KEYRING:
			return kFlagLockKeyring;
		case KEY_STORE_GET_NEXT_APPLICATION:
			return kFlagEnumerateApplications;
		case KEY_STORE_REMOVE_APPLICATION:
			return kFlagRemoveApplications;
	}

	return 0;
}


const char*
KeyStoreServer::_AccessStringFor(uint32 accessFlag) const
{
	switch (accessFlag) {
		case kFlagGetKey:
			return "Get keys from the keyring.";
		case kFlagEnumerateKeys:
			return "Enumerate and get keys from the keyring.";
		case kFlagAddKey:
			return "Add keys to the keyring.";
		case kFlagRemoveKey:
			return "Remove keys from the keyring.";
		case kFlagAddKeyring:
			return "Add new keyrings.";
		case kFlagRemoveKeyring:
			return "Remove keyrings.";
		case kFlagEnumerateKeyrings:
			return "Enumerate the available keyrings.";
		case kFlagSetUnlockKey:
			return "Set the unlock key of the keyring.";
		case kFlagRemoveUnlockKey:
			return "Remove the unlock key of the keyring.";
		case kFlagAddKeyringsToMaster:
			return "Add the keyring key to the master keyring.";
		case kFlagRemoveKeyringsFromMaster:
			return "Remove the keyring key from the master keyring.";
		case kFlagEnumerateMasterKeyrings:
			return "Enumerate keyrings added to the master keyring.";
		case kFlagQueryLockState:
			return "Query the lock state of the keyring.";
		case kFlagLockKeyring:
			return "Lock the keyring.";
		case kFlagEnumerateApplications:
			return "Enumerate the applications of the keyring.";
		case kFlagRemoveApplications:
			return "Remove applications from the keyring.";
	}

	return NULL;
}


status_t
KeyStoreServer::_ResolveCallingApp(const BMessage& message,
	app_info& callingAppInfo) const
{
	team_id callingTeam = message.ReturnAddress().Team();
	status_t result = be_roster->GetRunningAppInfo(callingTeam,
		&callingAppInfo);
	if (result != B_OK)
		return result;

	// Do some sanity checks.
	if (callingAppInfo.team != callingTeam)
		return B_ERROR;

	return B_OK;
}


status_t
KeyStoreServer::_ValidateAppAccess(Keyring& keyring, const app_info& appInfo,
	uint32 accessFlags)
{
	BMessage appMessage;
	BPath path(&appInfo.ref);
	status_t result = keyring.FindApplication(appInfo.signature,
		path.Path(), appMessage);
	if (result != B_OK && result != B_ENTRY_NOT_FOUND)
		return result;

	// TODO: Implement running image checksum mechanism.
	BString checksum = path.Path();

	bool appIsNew = false;
	bool appWasUpdated = false;
	uint32 appFlags = 0;
	BString appSum = "";
	if (result == B_OK) {
		if (appMessage.FindUInt32("flags", &appFlags) != B_OK
			|| appMessage.FindString("checksum", &appSum) != B_OK) {
			appIsNew = true;
			appFlags = 0;
		} else if (appSum != checksum) {
			appWasUpdated = true;
			appFlags = 0;
		}
	} else
		appIsNew = true;

	if ((accessFlags & appFlags) == accessFlags)
		return B_OK;

	const char* accessString = _AccessStringFor(accessFlags);
	bool allowAlways = false;
	result = _RequestAppAccess(keyring.Name(), appInfo.signature, path.Path(),
		accessString, appIsNew, appWasUpdated, accessFlags, allowAlways);
	if (result != B_OK || !allowAlways)
		return result;

	appMessage.MakeEmpty();
	appMessage.AddString("path", path.Path());
	appMessage.AddUInt32("flags", appFlags | accessFlags);
	appMessage.AddString("checksum", checksum);

	keyring.RemoveApplication(appInfo.signature, path.Path());
	if (keyring.AddApplication(appInfo.signature, appMessage) == B_OK)
		_WriteKeyStoreDatabase();

	return B_OK;
}


status_t
KeyStoreServer::_RequestAppAccess(const BString& keyringName,
	const char* signature, const char* path, const char* accessString,
	bool appIsNew, bool appWasUpdated, uint32 accessFlags, bool& allowAlways)
{
	AppAccessRequestWindow* requestWindow
		= new(std::nothrow) AppAccessRequestWindow(keyringName, signature, path,
			accessString, appIsNew, appWasUpdated);
	if (requestWindow == NULL)
		return B_NO_MEMORY;

	return requestWindow->RequestAppAccess(allowAlways);
}


Keyring*
KeyStoreServer::_FindKeyring(const BString& name)
{
	if (name.IsEmpty() || name == kMasterKeyringName)
		return fMasterKeyring;

	return fKeyrings.BinarySearchByKey(name, &Keyring::Compare);
}


status_t
KeyStoreServer::_AddKeyring(const BString& name)
{
	if (_FindKeyring(name) != NULL)
		return B_NAME_IN_USE;

	Keyring* keyring = new(std::nothrow) Keyring(name);
	if (keyring == NULL)
		return B_NO_MEMORY;

	if (!fKeyrings.BinaryInsert(keyring, &Keyring::Compare)) {
		delete keyring;
		return B_ERROR;
	}

	return B_OK;
}


status_t
KeyStoreServer::_RemoveKeyring(const BString& name)
{
	Keyring* keyring = _FindKeyring(name);
	if (keyring == NULL)
		return B_ENTRY_NOT_FOUND;

	if (keyring == fMasterKeyring) {
		// The master keyring can't be removed.
		return B_NOT_ALLOWED;
	}

	return fKeyrings.RemoveItem(keyring) ? B_OK : B_ERROR;
}


status_t
KeyStoreServer::_UnlockKeyring(Keyring& keyring)
{
	if (!keyring.HasUnlockKey())
		return keyring.Unlock(NULL);

	// If we are accessing a keyring that has been added to master access we
	// get the key from the master keyring and unlock with that.
	BMessage keyMessage;
	if (&keyring != fMasterKeyring && fMasterKeyring->IsUnlocked()) {
		if (fMasterKeyring->FindKey(kKeyringKeysIdentifier, keyring.Name(),
				false, &keyMessage) == B_OK) {
			// We found a key for this keyring, try to unlock with it.
			if (keyring.Unlock(&keyMessage) == B_OK)
				return B_OK;
		}
	}

	// No key, we need to request one from the user.
	status_t result = _RequestKey(keyring.Name(), keyMessage);
	if (result != B_OK)
		return result;

	return keyring.Unlock(&keyMessage);
}


status_t
KeyStoreServer::_RequestKey(const BString& keyringName, BMessage& keyMessage)
{
	KeyRequestWindow* requestWindow = new(std::nothrow) KeyRequestWindow();
	if (requestWindow == NULL)
		return B_NO_MEMORY;

	return requestWindow->RequestKey(keyringName, keyMessage);
}


// --- Session password + encrypted key helpers ---

static inline void
secure_memzero_server(void* p, size_t n)
{
	volatile unsigned char* cp = (volatile unsigned char*)p;
	while (n--) *cp++ = 0;
}


status_t
KeyStoreServer::_GetOrAskSessionPassword()
{
	if (fHasSessionPassword)
		return B_OK;

	MasterPasswordRequestWindow* window
		= new(std::nothrow) MasterPasswordRequestWindow();
	if (window == NULL)
		return B_NO_MEMORY;

	BString password;
	status_t result = window->RequestPassword(password);
	if (result != B_OK)
		return result;

	if (password.IsEmpty())
		return B_BAD_VALUE;

	fSessionPassword = password;
	fHasSessionPassword = true;
	return B_OK;
}



status_t
KeyStoreServer::_EncryptKeyData(BMessage& keyMessage)
{
    fprintf(stderr, "[DEBUG SERVER] === INIZIO _EncryptKeyData (OpenSSL RSA) ===\n");

    // 1. Recuperiamo i dati in chiaro dal messaggio
    const void* plainData = NULL;
    ssize_t plainLen = 0;
    if (keyMessage.FindData("data", B_RAW_TYPE, &plainData, &plainLen) != B_OK) {
        fprintf(stderr, "[DEBUG SERVER] ERRORE: Nessun dato 'data' da cifrare trovato.\n");
        return B_BAD_VALUE;
    }

    // 2. Troviamo il percorso del file 'master' (dove risiede la chiave pubblica)
    BPath settingsDir;
    if (find_directory(B_USER_SETTINGS_DIRECTORY, &settingsDir) != B_OK) {
        return B_ERROR;
    }
    BPath keyPath(settingsDir.Path(), "system/keystore/master");

    BFile keyFile(keyPath.Path(), B_READ_ONLY);
    if (keyFile.InitCheck() != B_OK) {
        fprintf(stderr, "[DEBUG SERVER] ERRORE: Impossibile aprire il file master in lettura: %s\n", keyPath.Path());
        return keyFile.InitCheck();
    }

    // 3. Leggiamo la chiave pubblica DER dal file
    off_t fileSize = 0;
    keyFile.GetSize(&fileSize);
    if (fileSize <= 0) return B_BAD_DATA;

    unsigned char* pubKeyDer = new(std::nothrow) unsigned char[fileSize];
    if (pubKeyDer == NULL) return B_NO_MEMORY;

    if (keyFile.Read(pubKeyDer, fileSize) != fileSize) {
        fprintf(stderr, "[DEBUG SERVER] ERRORE: Lettura parziale della chiave pubblica.\n");
        delete[] pubKeyDer;
        return B_IO_ERROR;
    }

    // 4. Convertiamo il buffer DER in un oggetto EVP_PKEY di OpenSSL
    const unsigned char* p = pubKeyDer;
    EVP_PKEY* pubKey = d2i_PUBKEY(NULL, &p, fileSize);
    delete[] pubKeyDer;

    if (pubKey == NULL) {
        fprintf(stderr, "[DEBUG SERVER] ERRORE: d2i_PUBKEY fallito. Il file master è corrotto?\n");
        ERR_print_errors_fp(stderr);
        return B_BAD_DATA;
    }

    // 5. Inizializziamo il contesto di cifratura OpenSSL
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new(pubKey, NULL);
    EVP_PKEY_free(pubKey); // Controllato internamente dal contesto ora

    if (ctx == NULL || EVP_PKEY_encrypt_init(ctx) <= 0) {
        fprintf(stderr, "[DEBUG SERVER] ERRORE: Inizializzazione contesto di cifratura fallita.\n");
        EVP_PKEY_CTX_free(ctx);
        return B_ERROR;
    }

    // Impostiamo il padding RSA-OAEP con SHA-256 (lo standard crittografico moderno)
    if (EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_OAEP_PADDING) <= 0
            || EVP_PKEY_CTX_set_rsa_oaep_md(ctx, EVP_sha256()) <= 0) {
        fprintf(stderr, "[DEBUG SERVER] ERRORE: Configurazione padding OAEP fallita.\n");
        EVP_PKEY_CTX_free(ctx);
        return B_ERROR;
    }

    // 6. Calcoliamo la dimensione dell'output ed eseguiamo la cifratura
    size_t outLen = 0;
    if (EVP_PKEY_encrypt(ctx, NULL, &outLen, (const unsigned char*)plainData, plainLen) <= 0) {
        fprintf(stderr, "[DEBUG SERVER] ERRORE: Calcolo dimensione ciphertext fallito.\n");
        EVP_PKEY_CTX_free(ctx);
        return B_ERROR;
    }

    unsigned char* outBuf = new(std::nothrow) unsigned char[outLen];
    if (outBuf == NULL) {
        EVP_PKEY_CTX_free(ctx);
        return B_NO_MEMORY;
    }

    if (EVP_PKEY_encrypt(ctx, outBuf, &outLen, (const unsigned char*)plainData, plainLen) <= 0) {
        fprintf(stderr, "[DEBUG SERVER] ERRORE: Cifratura asimmetrica RSA fallita.\n");
        ERR_print_errors_fp(stderr);
        delete[] outBuf;
        EVP_PKEY_CTX_free(ctx);
        return B_ERROR;
    }

    EVP_PKEY_CTX_free(ctx);

    // 7. Aggiorniamo il BMessage con il blob asimmetrico
    keyMessage.RemoveName("data");
    keyMessage.AddData("data", B_RAW_TYPE, outBuf, outLen);
    
    // Rimuoviamo rimasugli di nonce/IV legati al vecchio codice AES simmetrico
    keyMessage.RemoveName("enc_nonce"); 
    keyMessage.SetBool("encrypted", true);

    delete[] outBuf;
    fprintf(stderr, "[DEBUG SERVER] Cifratura RSA completata con successo! Ciphertext len: %zu\n", outLen);
    fprintf(stderr, "[DEBUG SERVER] === FINE _EncryptKeyData ===\n");
    return B_OK;
}
status_t
KeyStoreServer::_DecryptKeyData(BMessage& keyMessage)
{
    fprintf(stderr, "[DEBUG CRYPTO-READ] === INIZIO _DecryptKeyData (RSA Asimmetrico) ===\n");

    // 1. Verifichiamo se il record è marcato come cifrato
    bool encrypted = false;
    if (keyMessage.FindBool("encrypted", &encrypted) != B_OK || !encrypted) {
        fprintf(stderr, "[DEBUG CRYPTO-READ] Chiave non marchiata come cifrata, esco.\n");
        return B_OK; 
    }

    // 2. Recuperiamo il ciphertext (il blob cifrato asimmetricamente)
    const void* encData = NULL;
    ssize_t encLen = 0;
    if (keyMessage.FindData("data", B_RAW_TYPE, &encData, &encLen) != B_OK) {
        fprintf(stderr, "[DEBUG CRYPTO-READ] ERRORE: Impossibile recuperare campo 'data' dal database!\n");
        return B_BAD_DATA;
    }

    fprintf(stderr, "[DEBUG CRYPTO-READ] Ciphertext RSA recuperato (Len: %" B_PRIdSSIZE "): %s\n", 
        encLen, _BufToHex((const uint8_t*)encData, encLen).c_str());

    // ==========================================================
    // ESTRAZIONE VOLATILE DELLA CHIAVE PRIVATA RSA (ON-DEMAND)
    // ==========================================================
    EVP_PKEY* privateKey = _DecryptMasterPrivateKey();
    if (privateKey == NULL) {
        fprintf(stderr, "[DEBUG CRYPTO-READ] ERRORE CRITICO: Impossibile sbloccare la chiave privata RSA dal master file!\n");
        return B_NOT_ALLOWED; 
    }
    fprintf(stderr, "[DEBUG CRYPTO-READ] Chiave privata RSA sbloccata correttamente ed estratta in RAM.\n");

    // 3. Inizializziamo il contesto di decifratura OpenSSL EVP
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new(privateKey, NULL);
    if (ctx == NULL || EVP_PKEY_decrypt_init(ctx) <= 0) {
        fprintf(stderr, "[DEBUG CRYPTO-READ] ERRORE: Inizializzazione contesto OpenSSL fallita.\n");
        ERR_print_errors_fp(stderr);
        EVP_PKEY_free(privateKey); // <--- Liberiamo subito la risorsa in memoria
        return B_ERROR;
    }

    // Configuriame lo stesso identico schema di padding usato in scrittura: RSA-OAEP con SHA-256
    if (EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_OAEP_PADDING) <= 0
            || EVP_PKEY_CTX_set_rsa_oaep_md(ctx, EVP_sha256()) <= 0) {
        fprintf(stderr, "[DEBUG CRYPTO-READ] ERRORE: Configurazione padding OAEP fallita.\n");
        EVP_PKEY_CTX_free(ctx);
        EVP_PKEY_free(privateKey);
        return B_ERROR;
    }

    // 4. Determiniamo la dimensione massima necessaria per il buffer in chiaro
    size_t outLen = 0;
    if (EVP_PKEY_decrypt(ctx, NULL, &outLen, (const unsigned char*)encData, encLen) <= 0) {
        fprintf(stderr, "[DEBUG CRYPTO-READ] ERRORE: Impossibile determinare la dimensione massima del plaintext.\n");
        ERR_print_errors_fp(stderr);
        EVP_PKEY_CTX_free(ctx);
        EVP_PKEY_free(privateKey);
        return B_BAD_DATA;
    }

    unsigned char* plainData = new(std::nothrow) unsigned char[outLen];
    if (plainData == NULL) {
        EVP_PKEY_CTX_free(ctx);
        EVP_PKEY_free(privateKey);
        return B_NO_MEMORY;
    }

    // 5. Eseguiamo la reale decifratura RSA asimmetrica
    status_t result = B_OK;
    if (EVP_PKEY_decrypt(ctx, plainData, &outLen, (const unsigned char*)encData, encLen) <= 0) {
        fprintf(stderr, "[DEBUG CRYPTO-READ] ERRORE DI DECIFRATURA RSA: Chiave errata o dati alterati!\n");
        ERR_print_errors_fp(stderr);
        secure_memzero_server(plainData, outLen);
        delete[] plainData;
        result = B_BAD_DATA;
    } else {
        // La decifratura è riuscita!
        fprintf(stderr, "[DEBUG CRYPTO-READ] DECIFRATURA RSA RIUSCITA! Dati recuperati (Len: %zu)\n", outLen);
        
        // 6. Aggiorniamo il BMessage con il testo in chiaro e ripuliamo i metadati
        keyMessage.RemoveName("data");
        keyMessage.AddData("data", B_RAW_TYPE, plainData, outLen);
        keyMessage.RemoveName("enc_nonce"); // Rimuove eventuali vecchi rimasugli simmetrici
        keyMessage.SetBool("encrypted", false);

        secure_memzero_server(plainData, outLen);
        delete[] plainData;
    }

    // ==========================================================
    // DISTRUZIONE IMMEDIATA DELLE CHIAVI DALLA MEMORIA VOLATILE
    // ==========================================================
    EVP_PKEY_CTX_free(ctx);
    EVP_PKEY_free(privateKey); // <--- La chiave privata RSA non esiste più nella RAM del server
    
    fprintf(stderr, "[DEBUG CRYPTO-READ] === FINE _DecryptKeyData (Memoria ripulita) ===\n");
    return result;
}

EVP_PKEY*
KeyStoreServer::_DecryptMasterPrivateKey()
{
    if (!fHasSessionPassword || fSessionPassword.IsEmpty()) {
        return NULL;
    }

    // ==========================================
    // LIVELLO 1: Recupero Salt dallo Shadow per KDF
    // ==========================================
    BPath settingsDir;
    if (find_directory(B_USER_SETTINGS_DIRECTORY, &settingsDir) != B_OK) return NULL;
    
    BPath shadowPath(settingsDir.Path(), "shadow");
    BFile shadowFile(shadowPath.Path(), B_READ_ONLY);
    if (shadowFile.InitCheck() != B_OK) return NULL;

    BMessage shadowMsg;
    if (shadowMsg.Unflatten(&shadowFile) != B_OK) return NULL;

    const void* shadowSalt = NULL;
    ssize_t saltLen = 0;
    // Estraiamo il Salt dall'unico BMessage dello shadow
    if (shadowMsg.FindData("salt", B_RAW_TYPE, &shadowSalt, &saltLen) != B_OK || saltLen != 16) {
        return NULL;
    }

    // Rigeneriamo la chiave AES-256 usando Password + Salt (1000 round SHA256)
    BCrypto crypto;
    if (crypto.InitCheck() != B_OK) return NULL;

    size_t passLen = fSessionPassword.Length();
    size_t inputLen = passLen + 16;
    uint8_t* kdfInput = new(std::nothrow) uint8_t[inputLen];
    if (kdfInput == NULL) return NULL;

    memcpy(kdfInput, fSessionPassword.String(), passLen);
    memcpy(kdfInput + passLen, shadowSalt, 16);

    uint8_t aesKey[32];
    status_t err = crypto.Digest(B_CRYPTO_SHA256, kdfInput, inputLen, aesKey);
    secure_memzero_server(kdfInput, inputLen);
    delete[] kdfInput;
    if (err != B_OK) return NULL;

    for (int i = 1; i < 1000; i++) {
        if (crypto.Digest(B_CRYPTO_SHA256, aesKey, 32, aesKey) != B_OK) return NULL;
    }

    // ==========================================
    // LIVELLO 2: Estrazione e Decifratura della Privata RSA
    // ==========================================
    BPath keyPath(settingsDir.Path(), "system/keystore/master");
    BFile keyFile(keyPath.Path(), B_READ_ONLY);
    if (keyFile.InitCheck() != B_OK) return NULL;

    attr_info attrInfo;
    if (keyFile.GetAttrInfo("crypto:private_key", &attrInfo) != B_OK) return NULL;

    uint8_t* attrData = new(std::nothrow) uint8_t[attrInfo.size];
    if (attrData == NULL) return NULL;

    if (keyFile.ReadAttr("crypto:private_key", B_RAW_TYPE, 0, attrData, attrInfo.size) != attrInfo.size) {
        delete[] attrData;
        return NULL;
    }

    // Separizziamo l'IV casuale (primi 16 byte) dal payload cifrato (il resto dell'attributo)
    uint8_t iv[16];
    memcpy(iv, attrData, 16);
    size_t encPrivLen = attrInfo.size - 16;
    uint8_t* encPriv = attrData + 16;

    crypto.SetAlgorithm(B_CRYPTO_AES);
    crypto.SetMode(B_CRYPTO_MODE_CBC);
    crypto.SetPadding(true, B_CRYPTO_PKCS7);

    uint8_t* privDer = new(std::nothrow) uint8_t[encPrivLen];
    if (privDer == NULL) {
        delete[] attrData;
        return NULL;
    }

    // Decifriamo l'RSA privata nativa in formato DER
    ssize_t privLen = crypto.Decrypt(aesKey, sizeof(aesKey), iv, sizeof(iv), encPriv, encPrivLen, privDer, encPrivLen);
    
    secure_memzero_server(aesKey, sizeof(aesKey));
    delete[] attrData;

    if (privLen < 0) {
        secure_memzero_server(privDer, encPrivLen);
        delete[] privDer;
        return NULL; 
    }

    // Convertiamo l'array DER nell'oggetto OpenSSL d2i_PrivateKey pronto per l'uso volatile
    const unsigned char* p = privDer;
    EVP_PKEY* privKey = d2i_PrivateKey(EVP_PKEY_RSA, NULL, &p, privLen);
    
    secure_memzero_server(privDer, privLen);
    delete[] privDer;

    return privKey; 
}

int
main(int argc, char* argv[])
{
	KeyStoreServer* app = new(std::nothrow) KeyStoreServer();
	if (app == NULL)
		return 1;

	app->Run();
	delete app;
	return 0;
}
