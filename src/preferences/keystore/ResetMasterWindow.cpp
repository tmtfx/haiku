/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include "ResetMasterWindow.h"
#include <TextControl.h>
#include <Button.h>
#include <TextView.h>
#include <LayoutBuilder.h>
#include <FindDirectory.h>
#include <Path.h>
#include <File.h>
#include <Directory.h>
#include <KeyStore.h>
#include <Alert.h>
#include <Catalog.h>
#include <openssl/evp.h>
#include <openssl/x509.h>
#include <openssl/rsa.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#include "crypto/BCrypto.h"
#include <vector>
#include <algorithm>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "ResetMasterWindow"

static const uint32 MSG_RESET_PASSWORD = 'RSTP';
static const uint32 MSG_CANCEL_RESET   = 'RSTC';

// TODO: RIMUOVERE FINITO IL DEBUG
#include <iomanip>
#include <sstream>

// Helper locale per convertire buffer binari in stringhe esadecimali per i log
static std::string _ShadowBufToHex(const uint8_t* buf, size_t len) {
    std::ostringstream ss;
    for (size_t i = 0; i < len; ++i)
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)buf[i];
    return ss.str();
}
// ************************************

ResetMasterWindow::ResetMasterWindow(BWindow* parent)
    : BWindow(BRect(150, 150, 500, 400), B_TRANSLATE("Reset Master Password"),
        B_TITLED_WINDOW, B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS),
      fParent(parent)
{
    BTextView* warningView = new BTextView("warning");
    warningView->SetText(B_TRANSLATE(
        "WARNING: Resetting the Master Password will overwrite the "
        "system's master password shadow file and generate a new Master Keypair "
        "on disk, similar to a fresh installation. This is a critical security "
        "operation. Any keys currently encrypted with the old master password "
        "will need to be re-entered."));
    warningView->MakeEditable(false);
    warningView->MakeSelectable(false);
    warningView->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
    rgb_color textColor = ui_color(B_PANEL_TEXT_COLOR);
    warningView->SetFontAndColor(be_plain_font, B_FONT_ALL, &textColor);

    fPasswordControl = new BTextControl("new_pass", B_TRANSLATE("New Master Password:"), "", nullptr);
    fPasswordControl->TextView()->HideTyping(true);

    fConfirmControl = new BTextControl("confirm_pass", B_TRANSLATE("Confirm Password:"), "", nullptr);
    fConfirmControl->TextView()->HideTyping(true);

    fResetButton = new BButton("reset", B_TRANSLATE("Reset"), new BMessage(MSG_RESET_PASSWORD));
    fCancelButton = new BButton("cancel", B_TRANSLATE("Cancel"), new BMessage(MSG_CANCEL_RESET));

    BLayoutBuilder::Group<>(this, B_VERTICAL, 10)
        .SetInsets(15)
        .Add(warningView)
        .Add(fPasswordControl)
        .Add(fConfirmControl)
        .AddGroup(B_HORIZONTAL, 10)
            .AddGlue()
            .Add(fCancelButton)
            .Add(fResetButton)
        .End()
    .End();

    fPasswordControl->MakeFocus(true);
}

ResetMasterWindow::~ResetMasterWindow()
{
}

void ResetMasterWindow::MessageReceived(BMessage* msg)
{
    switch (msg->what) {
        case MSG_RESET_PASSWORD:
            _OnReset();
            break;
        case MSG_CANCEL_RESET:
            PostMessage(B_QUIT_REQUESTED);
            break;
        default:
            BWindow::MessageReceived(msg);
            break;
    }
}

void ResetMasterWindow::_OnReset()
{
    BString password = fPasswordControl->Text();
    BString confirm = fConfirmControl->Text();

    if (password.IsEmpty()) {
        BAlert* alert = new BAlert(B_TRANSLATE("Error"),
            B_TRANSLATE("Password cannot be empty!"), B_TRANSLATE("OK"));
        alert->Go();
        return;
    }

    if (password != confirm) {
        BAlert* alert = new BAlert(B_TRANSLATE("Error"),
            B_TRANSLATE("Passwords do not match!"), B_TRANSLATE("OK"));
        alert->Go();
        return;
    }

    uint8 salt[16];
    status_t err = _WriteMasterPasswordShadow(salt);
    if (err == B_OK) {
        err = _WriteKeystore(salt);
    } else {
        memset(salt, 0, sizeof(salt));
        BAlert* alert = new BAlert(B_TRANSLATE("Error"),
            B_TRANSLATE("Failed to save master password shadow file."), B_TRANSLATE("OK"));
        alert->Go();
        return;
    }

    memset(salt, 0, sizeof(salt));

    if (err == B_OK) {
        // Notifichiamo anche il daemon locale impostando la chiave attiva
        BKeyStore store;
        BPasswordKey key(password.String(), B_KEY_PURPOSE_KEYRING, "");
        store.SetMasterUnlockKey(key);

        BAlert* alert = new BAlert(B_TRANSLATE("Success"),
            B_TRANSLATE("Master password reset and key pair regenerated successfully!"), B_TRANSLATE("OK"));
        alert->Go();
        PostMessage(B_QUIT_REQUESTED);
    } else {
        BString errorMsg;
        errorMsg.SetToFormat(B_TRANSLATE("Failed to store master encryption keys: %s"), strerror(err));
        BAlert* alert = new BAlert(B_TRANSLATE("Error"), errorMsg.String(), B_TRANSLATE("OK"));
        alert->Go();
    }
}

status_t ResetMasterWindow::_WriteMasterPasswordShadow(uint8* outSalt)
{
    fprintf(stderr, "\n[DEBUG SHADOW-WRITE] === INIZIO _WriteMasterPasswordShadow ===\n");

    BPath settingsDir;
    if (find_directory(B_USER_SETTINGS_DIRECTORY, &settingsDir) != B_OK) {
        fprintf(stderr, "[DEBUG SHADOW-WRITE] ERRORE: find_directory fallito!\n");
        return B_ERROR;
    }

    BCrypto crypto;
    if (crypto.InitCheck() != B_OK) {
        fprintf(stderr, "[DEBUG SHADOW-WRITE] ERRORE: Inizializzazione BCrypto fallita!\n");
        return B_ERROR;
    }

    const char* password = fPasswordControl->Text();
    size_t passLen = strlen(password);
    fprintf(stderr, "[DEBUG SHADOW-WRITE] Password immessa: \"%s\" (Lunghezza: %zu)\n", password, passLen);

    uint8 salt[16];
    status_t err = crypto.GetRandomBytes(salt, sizeof(salt));
    if (err != B_OK) {
        fprintf(stderr, "[DEBUG SHADOW-WRITE] ERRORE: Generazione random del salt fallita!\n");
        return err;
    }

    // Passiamo il salt a _WriteKeystore
    memcpy(outSalt, salt, sizeof(salt));
    fprintf(stderr, "[DEBUG SHADOW-WRITE] SALT unico generato (Hex): %s\n", _ShadowBufToHex(salt, 16).c_str());

    size_t inputLen = passLen + sizeof(salt);
    uint8* input = new(std::nothrow) uint8[inputLen];
    if (input == NULL) {
        memset(salt, 0, sizeof(salt));
        return B_NO_MEMORY;
    }
    
    memcpy(input, password, passLen);
    memcpy(input + passLen, salt, sizeof(salt));
    fprintf(stderr, "[DEBUG SHADOW-WRITE] Dati concatenati (Pass+Salt Hex): %s\n", _ShadowBufToHex(input, inputLen).c_str());

    uint8 hash[64];
    size_t hashLen = crypto.GetHashLength(B_CRYPTO_BLAKE2B);

    err = crypto.Digest(B_CRYPTO_BLAKE2B, input, inputLen, hash);

    memset(input, 0, inputLen);
    delete[] input;

    if (err != B_OK) {
        fprintf(stderr, "[DEBUG SHADOW-WRITE] ERRORE: crypto.Digest BLAKE2b fallito!\n");
        memset(salt, 0, sizeof(salt));
        memset(hash, 0, sizeof(hash));
        return err;
    }

    fprintf(stderr, "[DEBUG SHADOW-WRITE] HASH BLAKE2b risultante (Hex): %s\n", _ShadowBufToHex(hash, hashLen).c_str());

    BMessage shadow;
    shadow.AddData("salt", B_RAW_TYPE, salt, sizeof(salt));
    shadow.AddData("hash", B_RAW_TYPE, hash, hashLen);

    memset(salt, 0, sizeof(salt));
    memset(hash, 0, sizeof(hash));

    BPath shadowPath(settingsDir.Path(), "shadow");
    fprintf(stderr, "[DEBUG SHADOW-WRITE] Salvo il file shadow in: %s\n", shadowPath.Path());

    BFile shadowFile(shadowPath.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
    if (shadowFile.InitCheck() != B_OK) {
        return shadowFile.InitCheck();
    }

    err = shadow.Flatten(&shadowFile);
    fprintf(stderr, "[DEBUG SHADOW-WRITE] === FINE _WriteMasterPasswordShadow (Esito: %s) ===\n\n", (err == B_OK ? "OK" : "ERR"));

    return err;
}
/* con bcrypto
status_t ResetMasterWindow::_WriteKeystore(const uint8* salt)
{
    BPath settingsDir;
    if (find_directory(B_USER_SETTINGS_DIRECTORY, &settingsDir) != B_OK) {
        return B_ERROR;
    }

    BPath keystoreDir(settingsDir.Path(), "system/keystore");
    status_t err = create_directory(keystoreDir.Path(), 0755);
    if (err != B_OK && err != B_FILE_EXISTS) {
        return err;
    }

    BPath keyPath(keystoreDir.Path(), "master");

    BCrypto crypto;
    if (crypto.InitCheck() != B_OK) {
        return B_ERROR;
    }

    const char* password = fPasswordControl->Text();
    size_t passLen = strlen(password);
    size_t inputLen = passLen + 16;

    uint8* kdfInput = new(std::nothrow) uint8[inputLen];
    if (kdfInput == NULL) {
        return B_NO_MEMORY;
    }
    memcpy(kdfInput, password, passLen);
    memcpy(kdfInput + passLen, salt, 16);

    uint8 aesKey[32];
    err = crypto.Digest(B_CRYPTO_SHA256, kdfInput, inputLen, aesKey);
    memset(kdfInput, 0, inputLen);
    delete[] kdfInput;
    if (err != B_OK) {
        memset(aesKey, 0, sizeof(aesKey));
        return err;
    }

    for (int i = 1; i < 1000; i++) {
        err = crypto.Digest(B_CRYPTO_SHA256, aesKey, sizeof(aesKey), aesKey);
        if (err != B_OK) {
            memset(aesKey, 0, sizeof(aesKey));
            return err;
        }
    }

    EVP_PKEY* pkey = NULL;
    {
        EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL);
        if (ctx == NULL
                || EVP_PKEY_keygen_init(ctx) <= 0
                || EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 2048) <= 0
                || EVP_PKEY_keygen(ctx, &pkey) <= 0) {
            EVP_PKEY_CTX_free(ctx);
            memset(aesKey, 0, sizeof(aesKey));
            return B_ERROR;
        }
        EVP_PKEY_CTX_free(ctx);
    }

    unsigned char* pubDer = NULL;
    int pubLen = i2d_PUBKEY(pkey, &pubDer);
    if (pubLen <= 0 || pubDer == NULL) {
        EVP_PKEY_free(pkey);
        memset(aesKey, 0, sizeof(aesKey));
        return B_ERROR;
    }

    unsigned char* privDer = NULL;
    int privLen = i2d_PrivateKey(pkey, &privDer);
    EVP_PKEY_free(pkey);
    if (privLen <= 0 || privDer == NULL) {
        OPENSSL_free(pubDer);
        memset(aesKey, 0, sizeof(aesKey));
        return B_ERROR;
    }

    uint8 iv[16];
    err = crypto.GetRandomBytes(iv, sizeof(iv));
    if (err != B_OK) {
        OPENSSL_cleanse(privDer, privLen);
        OPENSSL_free(privDer);
        OPENSSL_free(pubDer);
        memset(aesKey, 0, sizeof(aesKey));
        return err;
    }

    crypto.SetAlgorithm(B_CRYPTO_AES);
    crypto.SetMode(B_CRYPTO_MODE_CBC);
    crypto.SetPadding(true, B_CRYPTO_PKCS7);

    size_t encBufSize = crypto.GetOutputSize(privLen, B_CRYPTO_ENCRYPT);
    uint8* encPriv = new(std::nothrow) uint8[encBufSize];
    if (encPriv == NULL) {
        OPENSSL_cleanse(privDer, privLen);
        OPENSSL_free(privDer);
        OPENSSL_free(pubDer);
        memset(aesKey, 0, sizeof(aesKey));
        return B_NO_MEMORY;
    }

    ssize_t encLen = crypto.Encrypt(aesKey, sizeof(aesKey), iv, sizeof(iv),
        privDer, privLen, encPriv, encBufSize);

    OPENSSL_cleanse(privDer, privLen);
    OPENSSL_free(privDer);
    memset(aesKey, 0, sizeof(aesKey));

    if (encLen < 0) {
        memset(encPriv, 0, encBufSize);
        delete[] encPriv;
        OPENSSL_free(pubDer);
        return B_ERROR;
    }

    BFile keyFile(keyPath.Path(), B_READ_WRITE | B_CREATE_FILE | B_ERASE_FILE);
    if (keyFile.InitCheck() != B_OK) {
        err = keyFile.InitCheck();
        OPENSSL_free(pubDer);
        memset(encPriv, 0, encBufSize);
        delete[] encPriv;
        return err;
    }

    keyFile.Write(pubDer, pubLen);
    OPENSSL_free(pubDer);

    size_t attrSize = sizeof(iv) + encLen;
    uint8* attrData = new(std::nothrow) uint8[attrSize];
    if (attrData == NULL) {
        memset(encPriv, 0, encBufSize);
        delete[] encPriv;
        return B_NO_MEMORY;
    }
    memcpy(attrData, iv, sizeof(iv));
    memcpy(attrData + sizeof(iv), encPriv, encLen);

    memset(encPriv, 0, encBufSize);
    delete[] encPriv;

    ssize_t attrWritten = keyFile.WriteAttr("crypto:private_key", B_RAW_TYPE, 0, attrData, attrSize);
    if (attrWritten < 0) {
        err = attrWritten;
    } else {
        err = B_OK;
    }

    memset(attrData, 0, attrSize);
    delete[] attrData;

    return err;
}*/
status_t
ResetMasterWindow::_WriteKeystore(const uint8* salt)
{
    BPath settingsDir;
    if (find_directory(B_USER_SETTINGS_DIRECTORY, &settingsDir) != B_OK) {
        return B_ERROR;
    }

    BPath keystoreDir(settingsDir.Path(), "system/keystore");
    status_t err = create_directory(keystoreDir.Path(), 0755);
    if (err != B_OK && err != B_FILE_EXISTS) {
        return err;
    }

    BPath keyPath(keystoreDir.Path(), "master");

    // ==========================================
    // 1. KDF MANUALE CON OPENSSL (1000 ROUND SHA256)
    // ==========================================
    const char* password = fPasswordControl->Text();
    size_t passLen = strlen(password);
    size_t inputLen = passLen + 16;

    uint8* kdfInput = new(std::nothrow) uint8[inputLen];
    if (kdfInput == NULL) return B_NO_MEMORY;
    
    memcpy(kdfInput, password, passLen);
    memcpy(kdfInput + passLen, salt, 16);

    uint8 aesKey[32];
    unsigned int mdLen = 0;
    
    // Primo round SHA256
    EVP_Digest(kdfInput, inputLen, aesKey, &mdLen, EVP_sha256(), NULL);
    OPENSSL_cleanse(kdfInput, inputLen);
    delete[] kdfInput;

    // Restanti 999 round
    for (int i = 1; i < 1000; i++) {
        EVP_Digest(aesKey, 32, aesKey, &mdLen, EVP_sha256(), NULL);
    }

    // ==========================================
    // 2. GENERAZIONE COPPIA DI CHIAVI RSA-2048
    // ==========================================
    EVP_PKEY* pkey = NULL;
    EVP_PKEY_CTX* kctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL);
    if (kctx == NULL
            || EVP_PKEY_keygen_init(kctx) <= 0
            || EVP_PKEY_CTX_set_rsa_keygen_bits(kctx, 2048) <= 0
            || EVP_PKEY_keygen(kctx, &pkey) <= 0) {
        EVP_PKEY_CTX_free(kctx);
        OPENSSL_cleanse(aesKey, sizeof(aesKey));
        return B_ERROR;
    }
    EVP_PKEY_CTX_free(kctx);

    // Estrarre Chiave Pubblica in formato DER nudo
    unsigned char* pubDer = NULL;
    int pubLen = i2d_PUBKEY(pkey, &pubDer);
    if (pubLen <= 0 || pubDer == NULL) {
        EVP_PKEY_free(pkey);
        OPENSSL_cleanse(aesKey, sizeof(aesKey));
        return B_ERROR;
    }

    // Estrarre Chiave Privata in formato DER nudo
    unsigned char* privDer = NULL;
    int privLen = i2d_PrivateKey(pkey, &privDer);
    EVP_PKEY_free(pkey);
    if (privLen <= 0 || privDer == NULL) {
        OPENSSL_free(pubDer);
        OPENSSL_cleanse(aesKey, sizeof(aesKey));
        return B_ERROR;
    }

    // ==========================================
    // 3. CIFRATURA SIMMETRICA AES-256-CBC CON OPENSSL
    // ==========================================
    uint8 iv[16];
    if (RAND_bytes(iv, sizeof(iv)) != 1) {
        OPENSSL_cleanse(privDer, privLen);
        OPENSSL_free(privDer);
        OPENSSL_free(pubDer);
        OPENSSL_cleanse(aesKey, sizeof(aesKey));
        return B_ERROR;
    }

    // Allocazione buffer ciphertext (Dimensione massima = plaintext + blocco AES)
    int maxEncLen = privLen + 16; 
    uint8* encPriv = new(std::nothrow) uint8[maxEncLen];
    if (encPriv == NULL) {
        OPENSSL_cleanse(privDer, privLen);
        OPENSSL_free(privDer);
        OPENSSL_free(pubDer);
        OPENSSL_cleanse(aesKey, sizeof(aesKey));
        return B_NO_MEMORY;
    }

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    int len = 0;
    int encLen = 0;

    EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, aesKey, iv);
    EVP_CIPHER_CTX_set_padding(ctx, 1); // Abilita PKCS7

    if (EVP_EncryptUpdate(ctx, encPriv, &len, privDer, privLen) != 1) {
        fprintf(stderr, "[DEBUG WRITE] Errore in EncryptUpdate\n");
    }
    encLen = len;

    if (EVP_EncryptFinal_ex(ctx, encPriv + len, &len) != 1) {
        fprintf(stderr, "[DEBUG WRITE] Errore in EncryptFinal\n");
        encLen = -1;
    } else {
        encLen += len;
    }

    EVP_CIPHER_CTX_free(ctx);
    OPENSSL_cleanse(privDer, privLen);
    OPENSSL_free(privDer);
    OPENSSL_cleanse(aesKey, sizeof(aesKey));

    if (encLen < 0) {
        delete[] encPriv;
        OPENSSL_free(pubDer);
        return B_ERROR;
    }

    // ==========================================
    // 4. SCRITTURA SU DISCO (FILE + ATTRIBUTO BFS)
    // ==========================================
    BFile keyFile(keyPath.Path(), B_READ_WRITE | B_CREATE_FILE | B_ERASE_FILE);
    if (keyFile.InitCheck() != B_OK) {
        err = keyFile.InitCheck();
        OPENSSL_free(pubDer);
        delete[] encPriv;
        return err;
    }

    // Scrittura della chiave pubblica nel corpo del file
    keyFile.Write(pubDer, pubLen);
    OPENSSL_free(pubDer);

    // Preparazione pacchetto BFS: IV (16) + Ciphertext (encLen)
    size_t attrSize = sizeof(iv) + encLen;
    uint8* attrData = new(std::nothrow) uint8[attrSize];
    if (attrData == NULL) {
        delete[] encPriv;
        return B_NO_MEMORY;
    }
    
    memcpy(attrData, iv, sizeof(iv));
    memcpy(attrData + sizeof(iv), encPriv, encLen);
    delete[] encPriv;

    ssize_t attrWritten = keyFile.WriteAttr("crypto:private_key", B_RAW_TYPE, 0, attrData, attrSize);
    if (attrWritten < 0) {
        err = attrWritten;
    } else {
        err = B_OK;
        fprintf(stderr, "[DEBUG WRITE] Scrittura completata. PrivKey DER puro cifrato!\n");
    }

    OPENSSL_cleanse(attrData, attrSize);
    delete[] attrData;

    return err;
}
