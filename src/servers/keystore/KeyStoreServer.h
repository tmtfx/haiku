/*
 * Copyright 2012, Michael Lotz, mmlr@mlotz.ch. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _KEY_STORE_SERVER_H
#define _KEY_STORE_SERVER_H


#include <Application.h>
#include <File.h>
#include <Key.h>
#include <ObjectList.h>
#include <String.h>

struct app_info;
class BPath;
class Keyring;

typedef struct evp_pkey_st EVP_PKEY;
typedef BObjectList<Keyring, true> KeyringList;


class KeyStoreServer : public BApplication {
public:
									KeyStoreServer();
virtual								~KeyStoreServer();

virtual	void						MessageReceived(BMessage* message);

private:
		status_t					_ReadKeyStoreDatabase();
		status_t					_WriteKeyStoreDatabase();

		uint32						_AccessFlagsFor(uint32 command) const;
		const char*					_AccessStringFor(uint32 accessFlag) const;
		status_t					_ResolveCallingApp(const BMessage& message,
										app_info& callingAppInfo) const;

		status_t					_ValidateAppAccess(Keyring& keyring,
										const app_info& appInfo,
										uint32 accessFlags);
		status_t					_RequestAppAccess(
										const BString& keyringName,
										const char* signature,
										const char* path,
										const char* accessString, bool appIsNew,
										bool appWasUpdated, uint32 accessFlags,
										bool& allowAlways);

		Keyring*					_FindKeyring(const BString& name);

		status_t					_AddKeyring(const BString& name);
		status_t					_RemoveKeyring(const BString& name);

		status_t					_UnlockKeyring(Keyring& keyring);

		status_t					_RequestKey(const BString& keyringName,
										BMessage& keyMessage);

		// Session password: cached in RAM on first encrypted-key access.
		// Cleared when the server shuts down.
		status_t					_GetSalt(uint8* saltOut);
		status_t					_GetHash(uint8* hashOut);
		status_t					_GetShadow(uint8* saltOut, uint8* hashOut);
		status_t					_GetOrAskSessionPassword();

		// Fingerprint-unlock escrow (Fase 1e revisionata): K lives only in
		// fingerprint_server, gated on a live ordered 4-finger match;
		// AES(K, master password) lives on a user-chosen removable USB
		// stick, never on the internal disk - see the project plan for the
		// full rationale (the original single-disk-escrow design was found
		// insecure). This class only owns the stick's identity (a random
		// 16-byte id, stored both on the stick and in our own settings)
		// and the AES encrypt/decrypt of the master password itself.
		status_t					_EnableFingerprintUnlock(
											const int32* sequence,
											dev_t stickDevice);
		status_t					_DisableFingerprintUnlock();
		status_t					_ListRemovableVolumes(BMessage& reply);
		status_t					_StickIdPath(BPath& outPath);
		status_t					_SaveStickId(const uint8* stickId);
		status_t					_LoadStickId(uint8* outStickId);
		status_t					_ClearStickId();
		// Scans mounted removable, non-boot volumes for our marker file
		// whose stored stickId matches the one in our own settings.
		// B_ENTRY_NOT_FOUND if the stick isn't currently reachable - not
		// an error, just "fall back to the typed password".
		status_t					_FindFingerprintStick(BPath& outMarkerPath);
		status_t					_EncryptKeyData(BMessage& keyMessage);
		status_t					_DecryptKeyData(BMessage& keyMessage);
		EVP_PKEY*					_DecryptMasterPrivateKey();

		Keyring*					fMasterKeyring;
		KeyringList					fKeyrings;
		BFile						fKeyStoreFile;

		BString						fSessionPassword;
		bool						fHasSessionPassword;
		bool						fSessionPasswordValidated;
};


#endif // _KEY_STORE_SERVER_H
