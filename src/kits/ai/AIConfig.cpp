#include "AIConfig.h"
#include <FindDirectory.h>
#include <Path.h>
#include <File.h>
#include <Message.h>
#include <Roster.h>
#include <stdio.h>

#include <KeyStore.h>
#include <Key.h>

static const char* kAIKeyring = "AIService";
static const char* kAPIIdentifier = "api_key";
static const char* kServerSignature = "application/x-vnd.Haiku-ai_server";

static status_t _EnsureServer(BMessenger& outMessenger)
{
    outMessenger = BMessenger(kServerSignature);
    if (!outMessenger.IsValid()) {
        status_t res = be_roster->Launch(kServerSignature);
        // B_ALREADY_RUNNING non è un errore: il server è già attivo
        if (res != B_OK && res != B_ALREADY_RUNNING)
            return res;
        for (int i = 0; i < 10; i++) {
            usleep(200000);
            outMessenger = BMessenger(kServerSignature);
            if (outMessenger.IsValid()) break;
        }
    }
    return outMessenger.IsValid() ? B_OK : B_TIMED_OUT;
}

status_t AIGetAvailablePlugins(const char* engineType, BMessage& outPlugins)
{
    BMessenger server;
    status_t err = _EnsureServer(server);
    if (err != B_OK) return err;

    BMessage reply;
    BMessage m('LIST');
    const char* reqType = (engineType != nullptr && engineType[0] != '\0') ? engineType : "all";
    m.AddString("requested_type", reqType);

    err = server.SendMessage(&m, &reply, 5000000); // 5s timeout

    // B_BAD_PORT_ID: la porta nel BMessenger è stale (server avviato fuori
    // dal launch_daemon). Aspettiamo che il launch_daemon avvii l'istanza
    // corretta e riproviamo una volta sola.
    if (err == B_BAD_PORT_ID) {
        fprintf(stderr, "[libai_api] AIGetAvailablePlugins: porta stale (B_BAD_PORT_ID), riprovo via launch_daemon\n");
        usleep(500000);
        err = _EnsureServer(server);
        if (err == B_OK)
            err = server.SendMessage(&m, &reply, 5000000);
    }

    if (err != B_OK) return err;

    if (reply.FindMessage("plugins", &outPlugins) == B_OK)
        return B_OK;

    return B_BAD_DATA;
}

status_t AIGetPluginModels(const char* pluginName, BString& outJsonModels)
{
    if (!pluginName) return B_BAD_VALUE;

    BMessenger server;
    status_t err = _EnsureServer(server);
    if (err != B_OK) return err;

    BMessage reply;
    BMessage m('GMOD');
    m.AddString("plugin_name", pluginName);

    err = server.SendMessage(&m, &reply, 8000000); // 8s timeout (può richiedere rete)
    if (err != B_OK) return err;

    const char* json = nullptr; //TODO: non sarebbe il caso di passare ai BMessage invece di gestire i json fuori dal plugin?
    if (reply.FindString("plugin_models", &json) == B_OK) {
        outJsonModels.SetTo(json);
        return B_OK;
    }

    return B_BAD_DATA;
}

// Helper: read API key from KeyStore under keyring kAIKeyring with secondary=engine
static bool _GetAPIKeyFromKeyStore(const char* pluginName, BString& out)
{
    BKeyStore keyStore;
    BPasswordKey password;
    password.SetTo("", B_KEY_PURPOSE_GENERIC, kAPIIdentifier, pluginName);
    status_t res = keyStore.GetEncryptedKey(kAIKeyring, B_KEY_TYPE_PASSWORD,
        kAPIIdentifier, pluginName, password);
    if (res != B_OK) return false;
    const char* pwd = password.Password();
    if (!pwd) return false;
    out.SetTo(pwd);
    return true;
}

// Helper: store or remove API key
static bool _StoreAPIKeyToKeyStore(const char* engine, const char* apiKey)
{
    fprintf(stderr, "Requested _StoreAPIKeyToKeyStore con motore %s e apiKey %s\n", engine, apiKey);
    BKeyStore keyStore;
    // If empty apiKey -> remove existing
    if (!apiKey || apiKey[0] == '\0') {
    	fprintf(stderr,"Cancello la password visto che apiKey è vuoto\n");
        BPasswordKey existing;
        existing.SetTo("", B_KEY_PURPOSE_GENERIC, kAPIIdentifier, engine);
        if (keyStore.GetEncryptedKey(kAIKeyring, B_KEY_TYPE_PASSWORD,
                kAPIIdentifier, engine, existing) == B_OK) {
            fprintf(stderr,"la chiave cifrata esiste, procedo a rimuovere...\n");
            status_t r = keyStore.RemoveKey(kAIKeyring, existing);
            if (r == B_OK) {
                fprintf(stderr,"chiave rimossa con successo\n");
            } else {
                fprintf(stderr, "impossibile rimuovere la chiave\n");
            }
            return r == B_OK;
        } else {
            fprintf(stderr,"la chiave cifrata non esiste, non resta altro da fare, sicuramente non memorizzo una apiKey vuota\n");
        }
        return true; // nothing to remove
    }
    
    status_t r;
    // Remove existing if present
    fprintf(stderr,"Cancello la password precedente, se esiste...\n");
    BPasswordKey existing;
    existing.SetTo("", B_KEY_PURPOSE_GENERIC, kAPIIdentifier, engine);
    if (keyStore.GetEncryptedKey(kAIKeyring, B_KEY_TYPE_PASSWORD,
            kAPIIdentifier, engine, existing) == B_OK) {
        fprintf(stderr,"la chiave cifrata esiste, procedo a rimuovere...\n");
        r = keyStore.RemoveKey(kAIKeyring, existing);
        if (r == B_OK) {
                fprintf(stderr,"chiave rimossa con successo\n");
        } else {
                fprintf(stderr, "impossibile rimuovere la chiave\n");
        }
    } else {
    	fprintf(stderr, "GetEncryptedKey non è riuscito a ottenere la password esistente\n");
    }

    // Ensure keyring exists (ignore error if already exists)
    keyStore.AddKeyring(kAIKeyring);

    // Use BPasswordKey with purpose GENERIC to avoid implying web-only usage
    BPasswordKey pw;
    r = pw.EncryptedSetTo(apiKey, B_KEY_PURPOSE_GENERIC, kAPIIdentifier, engine);
    if (r == B_OK) {
                fprintf(stderr,"EncryptedSetTo andata a buon fine\n");
    } else {
                fprintf(stderr, "EncryptedSetTo ha fallito nel suo intento\n");
    }
    r = keyStore.AddEncryptedKey(kAIKeyring, pw);
    if (r == B_OK) {
                fprintf(stderr,"AddEncryptedKey andata a buon fine\n");
    } else {
                fprintf(stderr, "AddEncryptedKey ha fallito nel suo intento\n");
    }
    return r == B_OK;
}

// Check whether an API key exists for the given engine
bool HasAPIKey(const char* plugin)
{
    BString dummy;
    return GetPluginAPIKey(plugin, dummy);
}

bool GetPluginAPIKey(const char* pluginName, BString& outKey)
{
    if (!pluginName || pluginName[0] == '\0') {
        outKey.SetTo("");
        return false;
    }
    
    // Sfrutta l'helper statico esistente che interroga il KeyStore
    return _GetAPIKeyFromKeyStore(pluginName, outKey);
}

// Remove the API key for engine/plugin
bool RemoveAPIKey(const char* plugin)
{
    return _StoreAPIKeyToKeyStore(plugin, "");
}

// Rotate (replace) the API key for engine with newKey
bool RotateAPIKey(const char* engine, const char* newKey)
{
    if (!newKey) return false;
    // Overwrite existing entry atomically by removing then adding
    BKeyStore ks;
    BPasswordKey existing;
    if (ks.GetEncryptedKey(kAIKeyring, B_KEY_TYPE_PASSWORD, kAPIIdentifier,
            engine, existing) == B_OK) {
        ks.RemoveKey(kAIKeyring, existing);
    }
    // ensure keyring exists
    ks.AddKeyring(kAIKeyring);
    BPasswordKey pw;
    pw.EncryptedSetTo(newKey, B_KEY_PURPOSE_GENERIC, kAPIIdentifier, engine);
    return ks.AddEncryptedKey(kAIKeyring, pw) == B_OK;
}

bool LoadAISettings(AISettings& out)
{
    BPath path;
    if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK) return false;
    path.Append("AIService_settings");

    BFile file(path.Path(), B_READ_ONLY);
    if (file.InitCheck() != B_OK) return false;

    BMessage m;
    if (m.Unflatten(&file) != B_OK) return false;

    const char* s = nullptr;
    if (m.FindString("engine", &s) == B_OK) out.engine = s;
    if (m.FindString("plugin", &s) == B_OK) out.plugin = s;
    if (m.FindString("model", &s) == B_OK) out.model = s;

    // API key is stored in KeyStore, not in the settings file
    BString api;
    if (_GetAPIKeyFromKeyStore(out.plugin.String(), api)) {
        out.api_key = api;
    } else {
        out.api_key.SetTo("");
    }
    
    if (m.FindBool("use_remote_context", &out.use_remote_context) != B_OK) {
        out.use_remote_context = false; 
    }
    if (m.FindInt32("mcp_permissions", (int32*)&out.mcp_permissions) != B_OK) {
        out.mcp_permissions = 0;
    }
    return true;
}

bool SaveAISettings(const AISettings& settings)
{
    BPath path;
    if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK) return false;
    path.Append("AIService_settings");

    BFile file(path.Path(), B_READ_WRITE | B_CREATE_FILE | B_ERASE_FILE);
    if (file.InitCheck() != B_OK) return false;

    BMessage m('AISC');
    m.AddString("engine", settings.engine.String());
    m.AddString("plugin", settings.plugin.String());
    m.AddString("model", settings.model.String());
    m.AddBool("use_remote_context", settings.use_remote_context);
    m.AddInt32("mcp_permissions", (int32)settings.mcp_permissions);
    // Do NOT store api_key in the settings file. Store it securely in KeyStore.

    if (m.Flatten(&file) != B_OK) return false;

    // Store API key in KeyStore (can be empty to remove)
    if (!_StoreAPIKeyToKeyStore(settings.plugin.String(), settings.api_key.String())) {
        // Key store failure is non-fatal for settings file, but report failure
        return false;
    }

    return true;
}
