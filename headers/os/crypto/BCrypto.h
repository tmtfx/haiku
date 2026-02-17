/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef _BCRYPTO_H
#define _BCRYPTO_H

#include <SupportDefs.h>
#include <os/kernel/OS.h>
#include <crypto/BCryptoDefs.h>
#include <DataIO.h>

class BCrypto {
public:
								BCrypto();
	virtual						~BCrypto();

			status_t			InitCheck() const;


			status_t			GetRandomBytes(void* buffer, size_t len);
			void                SetPadding(bool enable, BCryptoPaddingType type = B_CRYPTO_PKCS7);
			size_t              GetOutputSize(size_t inputLen, BCryptoOperation op);
			// Shorthand methods
			ssize_t             Decrypt(uint8* key, size_t keyLen,
			                            uint8* iv, size_t ivLen,
                                        const void* in, size_t inLen,
                                        void* out, size_t outSize);
			ssize_t             Encrypt(uint8* key, size_t keyLen,
                                        uint8* iv, size_t ivLen,
                                        const void* in, size_t inLen,
                                        void* out, size_t outSize);
			status_t			Process(BCryptoUserRequest& userReq);
			size_t              GetHashLength(BCryptoAlgorithmID algo) const;
			status_t            Digest(BCryptoAlgorithmID algo, const void* data,
			                           size_t len, void* outHash);
			status_t            Digest(BCryptoAlgorithmID algo, BDataIO* source,
			                           void* outHash);
			void                SetAlgorithm(BCryptoAlgorithmID algo);
			void                SetMode(BCryptoMode mode);
			

private:
			int					fFd;
			bool                fPaddingEnabled;
			BCryptoPaddingType  fPaddingType;
			BCryptoAlgorithmID  fAlgorithm;
			BCryptoMode         fMode;
			uint8               fLastBlockBuffer[16];
			size_t fBufferSize;
            void                _FillRequest(BCryptoUserRequest& req, BCryptoOperation op,
                                             BCryptoAlgorithmID algo, BCryptoMode mode,
                                             uint8* key, size_t keyLen,
                                             uint8* iv, size_t ivLen, 
                                             iovec* src, iovec* dst,
                                             int vCount);
            void                _ApplyPadding(uint8* buffer, size_t inputLen, size_t totalLen);
            size_t              _RemovePadding(uint8* buffer, size_t len);
};

#endif // _BCRYPTO_H
