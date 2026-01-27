/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef _BCRYPTO_H
#define _BCRYPTO_H

#include <SupportDefs.h>
#include <os/kernel/OS.h>
#include <crypto/BCryptoDefs.h> // Il tuo file con le costanti

class BCrypto {
public:
								BCrypto();
	virtual						~BCrypto();

			status_t			InitCheck() const;

			// Metodi semplificati
			status_t			GetRandomBytes(void* buffer, size_t len);
			
			status_t			Encrypt(uint8* key, size_t keyLen,
									uint8* iv, size_t ivLen,
									const void* in, void* out, size_t len);
									
			status_t			Decrypt(uint8* key, size_t keyLen,
									uint8* iv, size_t ivLen,
									const void* in, void* out, size_t len);
			status_t			Process(BCryptoUserRequest& userReq);

private:
			int					fFd;
			status_t			_DoOperation(uint32 op, uint8* key, 
									size_t keyLen, uint8* iv, size_t ivLen,
									const void* in, void* out, size_t len);
};

#endif // _BCRYPTO_H
