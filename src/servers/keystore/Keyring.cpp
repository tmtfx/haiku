/*
 * Copyright 2012, Michael Lotz, mmlr@mlotz.ch. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */


#include "Keyring.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <new>

#include <crypto/BCrypto.h>
#include <crypto/BCryptoDefs.h>

static inline void secure_memzero(void* ptr, size_t len)
{
	volatile unsigned char *p = (volatile unsigned char *)ptr;
	while (len--) *p++ = 0;
}


// Derive a 32-byte AES-256 key from a password and a 16-byte salt using
// iterated SHA-256 (1000 rounds). Simple, no external library required.
static status_t
derive_key(const uint8_t* pwd, size_t pwdLen,
           const uint8_t* salt, size_t saltLen,
           uint8_t* outKey32)
{
	BCrypto crypto;
	if (crypto.InitCheck() != B_OK)
		return B_ERROR;

	size_t inputLen = pwdLen + saltLen;
	uint8_t* input = new(std::nothrow) uint8_t[inputLen];
	if (input == NULL)
		return B_NO_MEMORY;

	memcpy(input, pwd, pwdLen);
	memcpy(input + pwdLen, salt, saltLen);

	status_t err = crypto.Digest(B_CRYPTO_SHA256, input, inputLen, outKey32);
	secure_memzero(input, inputLen);
	delete[] input;
	if (err != B_OK)
		return err;

	for (int i = 1; i < 1000; i++) {
		err = crypto.Digest(B_CRYPTO_SHA256, outKey32, 32, outKey32);
		if (err != B_OK) {
			secure_memzero(outKey32, 32);
			return err;
		}
	}
	return B_OK;
}


Keyring::Keyring()
	:
	fHasUnlockKey(false),
	fUnlocked(false),
	fModified(false)
{
}


Keyring::Keyring(const char* name)
	:
	fName(name),
	fHasUnlockKey(false),
	fUnlocked(false),
	fModified(false)
{
}


Keyring::~Keyring()
{
}


status_t
Keyring::ReadFromMessage(const BMessage& message)
{
	status_t result = message.FindString("name", &fName);
	if (result != B_OK)
		return result;

	result = message.FindBool("hasUnlockKey", &fHasUnlockKey);
	if (result != B_OK)
		return result;

	if (message.GetBool("noData", false)) {
		fFlatBuffer.SetSize(0);
		return B_OK;
	}

	ssize_t size;
	const void* data;
	result = message.FindData("data", B_RAW_TYPE, &data, &size);
	if (result != B_OK)
		return result;

	if (size < 0)
		return B_ERROR;

	fFlatBuffer.SetSize(0);
	ssize_t written = fFlatBuffer.WriteAt(0, data, size);
	if (written != size) {
		fFlatBuffer.SetSize(0);
		return written < 0 ? written : B_ERROR;
	}

	return B_OK;
}


status_t
Keyring::WriteToMessage(BMessage& message)
{
	status_t result = _EncryptToFlatBuffer();
	if (result != B_OK)
		return result;

	if (fFlatBuffer.BufferLength() == 0)
		result = message.AddBool("noData", true);
	else {
		result = message.AddData("data", B_RAW_TYPE, fFlatBuffer.Buffer(),
			fFlatBuffer.BufferLength());
	}
	if (result != B_OK)
		return result;

	result = message.AddBool("hasUnlockKey", fHasUnlockKey);
	if (result != B_OK)
		return result;

	return message.AddString("name", fName);
}


status_t
Keyring::Unlock(const BMessage* keyMessage)
{
	if (fUnlocked)
		return B_OK;

	if (fHasUnlockKey == (keyMessage == NULL))
		return B_BAD_VALUE;

	if (keyMessage != NULL)
		fUnlockKey = *keyMessage;

	status_t result = _DecryptFromFlatBuffer();
	if (result != B_OK) {
		fUnlockKey.MakeEmpty();
		return result;
	}

	fUnlocked = true;
	return B_OK;
}


void
Keyring::Lock()
{
	if (!fUnlocked)
		return;

	_EncryptToFlatBuffer();

	fUnlockKey.MakeEmpty();
	fData.MakeEmpty();
	fApplications.MakeEmpty();
	fUnlocked = false;
}


bool
Keyring::IsUnlocked() const
{
	return fUnlocked;
}


bool
Keyring::HasUnlockKey() const
{
	return fHasUnlockKey;
}


const BMessage&
Keyring::UnlockKey() const
{
	return fUnlockKey;
}


status_t
Keyring::SetUnlockKey(const BMessage& keyMessage)
{
	if (!fUnlocked)
		return B_NOT_ALLOWED;

	fHasUnlockKey = true;
	fUnlockKey = keyMessage;
	fModified = true;
	return B_OK;
}


status_t
Keyring::RemoveUnlockKey()
{
	if (!fUnlocked)
		return B_NOT_ALLOWED;

	fUnlockKey.MakeEmpty();
	fHasUnlockKey = false;
	fModified = true;
	return B_OK;
}


status_t
Keyring::GetNextApplication(uint32& cookie, BString& signature,
	BString& path)
{
	if (!fUnlocked)
		return B_NOT_ALLOWED;

	char* nameFound = NULL;
	status_t result = fApplications.GetInfo(B_MESSAGE_TYPE, cookie++,
		&nameFound, NULL);
	if (result != B_OK)
		return B_ENTRY_NOT_FOUND;

	BMessage appMessage;
	result = fApplications.FindMessage(nameFound, &appMessage);
	if (result != B_OK)
		return B_ENTRY_NOT_FOUND;

	result = appMessage.FindString("path", &path);
	if (result != B_OK)
		return B_ERROR;

	signature = nameFound;
	return B_OK;
}


status_t
Keyring::FindApplication(const char* signature, const char* path,
	BMessage& appMessage)
{
	if (!fUnlocked)
		return B_NOT_ALLOWED;

	int32 count;
	type_code type;
	if (fApplications.GetInfo(signature, &type, &count) != B_OK)
		return B_ENTRY_NOT_FOUND;

	for (int32 i = 0; i < count; i++) {
		if (fApplications.FindMessage(signature, i, &appMessage) != B_OK)
			continue;

		BString appPath;
		if (appMessage.FindString("path", &appPath) != B_OK)
			continue;

		if (appPath == path)
			return B_OK;
	}

	appMessage.MakeEmpty();
	return B_ENTRY_NOT_FOUND;
}


status_t
Keyring::AddApplication(const char* signature, const BMessage& appMessage)
{
	if (!fUnlocked)
		return B_NOT_ALLOWED;

	status_t result = fApplications.AddMessage(signature, &appMessage);
	if (result != B_OK)
		return result;

	fModified = true;
	return B_OK;
}


status_t
Keyring::RemoveApplication(const char* signature, const char* path)
{
	if (!fUnlocked)
		return B_NOT_ALLOWED;

	if (path == NULL) {
		// We want all of the entries for this signature removed.
		status_t result = fApplications.RemoveName(signature);
		if (result != B_OK)
			return B_ENTRY_NOT_FOUND;

		fModified = true;
		return B_OK;
	}

	int32 count;
	type_code type;
	if (fApplications.GetInfo(signature, &type, &count) != B_OK)
		return B_ENTRY_NOT_FOUND;

	for (int32 i = 0; i < count; i++) {
		BMessage appMessage;
		if (fApplications.FindMessage(signature, i, &appMessage) != B_OK)
			return B_ERROR;

		BString appPath;
		if (appMessage.FindString("path", &appPath) != B_OK)
			continue;

		if (appPath == path) {
			fApplications.RemoveData(signature, i);
			fModified = true;
			return B_OK;
		}
	}

	return B_ENTRY_NOT_FOUND;
}


status_t
Keyring::FindKey(const BString& identifier, const BString& secondaryIdentifier,
	bool secondaryIdentifierOptional, BMessage* _foundKeyMessage) const
{
	if (!fUnlocked)
		return B_NOT_ALLOWED;

	int32 count;
	type_code type;
	if (fData.GetInfo(identifier, &type, &count) != B_OK)
		return B_ENTRY_NOT_FOUND;

	// We have a matching primary identifier, need to check for the secondary
	// identifier.
	for (int32 i = 0; i < count; i++) {
		BMessage candidate;
		if (fData.FindMessage(identifier, i, &candidate) != B_OK)
			return B_ERROR;

		BString candidateIdentifier;
		if (candidate.FindString("secondaryIdentifier",
				&candidateIdentifier) != B_OK) {
			candidateIdentifier = "";
		}

		if (candidateIdentifier == secondaryIdentifier) {
			if (_foundKeyMessage != NULL)
				*_foundKeyMessage = candidate;
			return B_OK;
		}
	}

	// We didn't find an exact match.
	if (secondaryIdentifierOptional) {
		if (_foundKeyMessage == NULL)
			return B_OK;

		// The secondary identifier is optional, so we just return the
		// first entry.
		return fData.FindMessage(identifier, 0, _foundKeyMessage);
	}

	return B_ENTRY_NOT_FOUND;
}


status_t
Keyring::FindKey(BKeyType type, BKeyPurpose purpose, uint32 index,
	BMessage& _foundKeyMessage) const
{
	if (!fUnlocked)
		return B_NOT_ALLOWED;

	for (int32 keyIndex = 0;; keyIndex++) {
		int32 count = 0;
		char* identifier = NULL;
		if (fData.GetInfo(B_MESSAGE_TYPE, keyIndex, &identifier, NULL,
				&count) != B_OK) {
			break;
		}

		if (type == B_KEY_TYPE_ANY && purpose == B_KEY_PURPOSE_ANY) {
			// No need to inspect the actual keys.
			if ((int32)index >= count) {
				index -= count;
				continue;
			}

			return fData.FindMessage(identifier, index, &_foundKeyMessage);
		}

		// Go through the keys to check their type and purpose.
		for (int32 subkeyIndex = 0; subkeyIndex < count; subkeyIndex++) {
			BMessage subkey;
			if (fData.FindMessage(identifier, subkeyIndex, &subkey) != B_OK)
				return B_ERROR;

			bool match = true;
			if (type != B_KEY_TYPE_ANY) {
				BKeyType subkeyType;
				if (subkey.FindUInt32("type", (uint32*)&subkeyType) != B_OK)
					return B_ERROR;

				match = subkeyType == type;
			}

			if (match && purpose != B_KEY_PURPOSE_ANY) {
				BKeyPurpose subkeyPurpose;
				if (subkey.FindUInt32("purpose", (uint32*)&subkeyPurpose)
						!= B_OK) {
					return B_ERROR;
				}

				match = subkeyPurpose == purpose;
			}

			if (match) {
				if (index == 0) {
					_foundKeyMessage = subkey;
					return B_OK;
				}

				index--;
			}
		}
	}

	return B_ENTRY_NOT_FOUND;
}


status_t
Keyring::AddKey(const BString& identifier, const BString& secondaryIdentifier,
	const BMessage& keyMessage)
{
	if (!fUnlocked)
		return B_NOT_ALLOWED;

	// Check for collisions.
	if (FindKey(identifier, secondaryIdentifier, false, NULL) == B_OK)
		return B_NAME_IN_USE;

	// We're fine, just add the new key.
	status_t result = fData.AddMessage(identifier, &keyMessage);
	if (result != B_OK)
		return result;

	fModified = true;
	return B_OK;
}


status_t
Keyring::RemoveKey(const BString& identifier,
	const BMessage& keyMessage)
{
	if (!fUnlocked)
		return B_NOT_ALLOWED;

	int32 count;
	type_code type;
	if (fData.GetInfo(identifier, &type, &count) != B_OK)
		return B_ENTRY_NOT_FOUND;

	for (int32 i = 0; i < count; i++) {
		BMessage candidate;
		if (fData.FindMessage(identifier, i, &candidate) != B_OK)
			return B_ERROR;

		bool match = candidate.HasSameData(keyMessage);
		if (!match) {
			// Backward/forward compatible removal path: accept a match by
			// (type, secondaryIdentifier) for this identifier. This allows
			// callers to remove encrypted keys even when transport metadata
			// (e.g. nonce fields) is not present in the request message.
			BString candidateSecondary;
			BString requestSecondary;
			BKeyType candidateType;
			BKeyType requestType;
			if (candidate.FindString("secondaryIdentifier", &candidateSecondary) == B_OK
				&& keyMessage.FindString("secondaryIdentifier", &requestSecondary) == B_OK
				&& candidate.FindUInt32("type", (uint32*)&candidateType) == B_OK
				&& keyMessage.FindUInt32("type", (uint32*)&requestType) == B_OK
				&& candidateSecondary == requestSecondary
				&& candidateType == requestType) {
				match = true;
			}
		}
		if (!match)
			continue;

		status_t result = fData.RemoveData(identifier, i);
		if (result != B_OK)
			return result;

		fModified = true;
		return B_OK;
	}

	return B_ENTRY_NOT_FOUND;
}


int
Keyring::Compare(const Keyring* one, const Keyring* two)
{
	return strcmp(one->Name(), two->Name());
}


int
Keyring::Compare(const BString* name, const Keyring* keyring)
{
	return strcmp(name->String(), keyring->Name());
}


status_t
Keyring::_EncryptToFlatBuffer()
{
	if (!fModified)
		return B_OK;

	if (!fUnlocked)
		return B_NOT_ALLOWED;

	BMessage container;
	status_t result = container.AddMessage("data", &fData);
	if (result != B_OK)
		return result;

	result = container.AddMessage("applications", &fApplications);
	if (result != B_OK)
		return result;

	fFlatBuffer.SetSize(0);
	fFlatBuffer.Seek(0, SEEK_SET);

	result = container.Flatten(&fFlatBuffer);
	if (result != B_OK)
		return result;

	if (fHasUnlockKey) {
		// Encrypt with BCrypto: SHA-256 key derivation + AES-256-CBC-PKCS7
		// Format: magic(4) version(4) salt(16) iv(16) len(4) cipherdata
		const uint8_t* pwdData = NULL;
		ssize_t pwdLen = 0;
		if (fUnlockKey.FindData("data", B_RAW_TYPE, (const void**)&pwdData,
				&pwdLen) != B_OK || pwdLen <= 0) {
			return B_BAD_DATA;
		}

		BCrypto crypto;
		if (crypto.InitCheck() != B_OK)
			return B_ERROR;

		// Generate random salt and IV
		uint8_t salt[16], iv[16];
		if (crypto.GetRandomBytes(salt, sizeof(salt)) != B_OK
				|| crypto.GetRandomBytes(iv, sizeof(iv)) != B_OK) {
			return B_ERROR;
		}

		// Derive 32-byte AES-256 key
		uint8_t key[32];
		result = derive_key(pwdData, (size_t)pwdLen, salt, sizeof(salt), key);
		if (result != B_OK)
			return result;

		// Encrypt plain data with AES-256-CBC-PKCS7
		size_t plainLen = (size_t)fFlatBuffer.BufferLength();
		const uint8_t* plainPtr = (const uint8_t*)fFlatBuffer.Buffer();

		crypto.SetAlgorithm(B_CRYPTO_AES);
		crypto.SetMode(B_CRYPTO_MODE_CBC);
		crypto.SetPadding(true, B_CRYPTO_PKCS7);

		size_t cipherBufSize = crypto.GetOutputSize(plainLen, B_CRYPTO_ENCRYPT);
		uint8_t* cipher = new(std::nothrow) uint8_t[cipherBufSize];
		if (cipher == NULL) {
			secure_memzero(key, sizeof(key));
			return B_NO_MEMORY;
		}

		ssize_t cipherLen = crypto.Encrypt(key, sizeof(key), iv, sizeof(iv),
			plainPtr, plainLen, cipher, cipherBufSize);

		secure_memzero(key, sizeof(key));

		if (cipherLen < 0) {
			secure_memzero(cipher, cipherBufSize);
			delete[] cipher;
			return B_ERROR;
		}

		// Serialise header + ciphertext into fFlatBuffer
		uint32 magic   = 0x4B534543; // 'KSEC'
		uint32 version = 2;          // v2 = BCrypto-based KDF
		uint32 cipherLen32 = (uint32)cipherLen;

		size_t off = 0;
		fFlatBuffer.SetSize(0);
		fFlatBuffer.Seek(0, SEEK_SET);
		fFlatBuffer.WriteAt(off, &magic,       sizeof(magic));       off += sizeof(magic);
		fFlatBuffer.WriteAt(off, &version,     sizeof(version));     off += sizeof(version);
		fFlatBuffer.WriteAt(off, salt,         sizeof(salt));        off += sizeof(salt);
		fFlatBuffer.WriteAt(off, iv,           sizeof(iv));          off += sizeof(iv);
		fFlatBuffer.WriteAt(off, &cipherLen32, sizeof(cipherLen32)); off += sizeof(cipherLen32);
		fFlatBuffer.WriteAt(off, cipher,       cipherLen);

		secure_memzero(cipher, cipherBufSize);
		delete[] cipher;
	}

	fModified = false;
	return B_OK;
}


status_t
Keyring::_DecryptFromFlatBuffer()
{
	if (fFlatBuffer.BufferLength() == 0)
		return B_OK;

	if (fHasUnlockKey) {
		// Parse header: magic(4) version(4) salt(16) iv(16) len(4) cipherdata
		const size_t kHeaderSize = 4 + 4 + 16 + 16 + 4;
		if (fFlatBuffer.BufferLength() < kHeaderSize)
			return B_BAD_DATA;

		uint32 magic = 0, version = 0;
		size_t off = 0;
		fFlatBuffer.ReadAt(off, &magic,   sizeof(magic));   off += sizeof(magic);
		fFlatBuffer.ReadAt(off, &version, sizeof(version)); off += sizeof(version);

		if (magic != 0x4B534543) // 'KSEC'
			return B_BAD_DATA;
		if (version != 2) // only v2 (BCrypto-based) supported
			return B_BAD_DATA;

		uint8_t salt[16], iv[16];
		fFlatBuffer.ReadAt(off, salt, sizeof(salt)); off += sizeof(salt);
		fFlatBuffer.ReadAt(off, iv,   sizeof(iv));   off += sizeof(iv);

		uint32 cipherLen32 = 0;
		fFlatBuffer.ReadAt(off, &cipherLen32, sizeof(cipherLen32));
		off += sizeof(cipherLen32);

		if (fFlatBuffer.BufferLength() < off + cipherLen32)
			return B_BAD_DATA;

		uint8_t* cipher = new(std::nothrow) uint8_t[cipherLen32];
		if (cipher == NULL)
			return B_NO_MEMORY;
		fFlatBuffer.ReadAt(off, cipher, cipherLen32);

		// Retrieve stored password
		const uint8_t* pwdData = NULL;
		ssize_t pwdLen = 0;
		if (fUnlockKey.FindData("data", B_RAW_TYPE, (const void**)&pwdData,
				&pwdLen) != B_OK || pwdLen <= 0) {
			secure_memzero(cipher, cipherLen32);
			delete[] cipher;
			return B_BAD_DATA;
		}

		// Re-derive AES-256 key
		uint8_t key[32];
		status_t err = derive_key(pwdData, (size_t)pwdLen,
			salt, sizeof(salt), key);
		if (err != B_OK) {
			secure_memzero(cipher, cipherLen32);
			delete[] cipher;
			return err;
		}

		// Decrypt AES-256-CBC-PKCS7
		BCrypto crypto;
		if (crypto.InitCheck() != B_OK) {
			secure_memzero(key, sizeof(key));
			secure_memzero(cipher, cipherLen32);
			delete[] cipher;
			return B_ERROR;
		}

		crypto.SetAlgorithm(B_CRYPTO_AES);
		crypto.SetMode(B_CRYPTO_MODE_CBC);
		crypto.SetPadding(true, B_CRYPTO_PKCS7);

		uint8_t* plain = new(std::nothrow) uint8_t[cipherLen32];
		if (plain == NULL) {
			secure_memzero(key, sizeof(key));
			secure_memzero(cipher, cipherLen32);
			delete[] cipher;
			return B_NO_MEMORY;
		}

		ssize_t plainLen = crypto.Decrypt(key, sizeof(key), iv, sizeof(iv),
			cipher, cipherLen32, plain, cipherLen32);

		secure_memzero(key, sizeof(key));
		secure_memzero(cipher, cipherLen32);
		delete[] cipher;

		if (plainLen < 0) {
			secure_memzero(plain, cipherLen32);
			delete[] plain;
			return B_BAD_DATA;
		}

		// Replace fFlatBuffer with decrypted plaintext for normal unflatten
		fFlatBuffer.SetSize(0);
		fFlatBuffer.Seek(0, SEEK_SET);
		fFlatBuffer.WriteAt(0, plain, plainLen);

		secure_memzero(plain, cipherLen32);
		delete[] plain;
	}

	BMessage container;
	fFlatBuffer.Seek(0, SEEK_SET);
	status_t result = container.Unflatten(&fFlatBuffer);
	if (result != B_OK)
		return result;

	result = container.FindMessage("data", &fData);
	if (result != B_OK)
		return result;

	result = container.FindMessage("applications", &fApplications);
	if (result != B_OK) {
		fData.MakeEmpty();
		return result;
	}

	return B_OK;
}
