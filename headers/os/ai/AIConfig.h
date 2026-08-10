#ifndef SRC_KITS_AI_AICONFIG_H
#define SRC_KITS_AI_AICONFIG_H

#include <String.h>
#include <Message.h>
#include <Messenger.h>
/*
enum ai_mcp_permissions {
    AI_PERM_SYSTEM_INFO     = 1 << 0,  // Permesso per recuperare info di sistema
    AI_PERM_FILE_SYSTEM     = 1 << 1,  // Permesso per leggere directory / file system
    AI_PERM_RUN_COMMANDS    = 1 << 2,  // Permesso per eseguire comandi generali (da usare con cautela)
    AI_PERM_SYSTEM_DEFAULT  = 0xFFFFFFFF // Usa le impostazioni globali della preflet
};
enum ai_mpc_perm_read {
	AI_PERM_FILE_READ_NO = 0,
	AI_PERM_FILE_READ_ASK,
	AI_PERM_FILE_READ_ALWAYS
};
enum ai_mpc_perm_WRITE {
	AI_PERM_FILE_WRITE_NO = 0,
	AI_PERM_FILE_WRITE_ASK,
	AI_PERM_FILE_WRITE_ALWAYS
};*/

/*
 * esempio d'uso: bool canReadAlways = (settings.mcp_permissions & AI_PERM_READ_MASK) == AI_PERM_READ_ALWAYS;
 */
enum ai_mcp_permissions {
    AI_PERM_SYSTEM_INFO     = 1 << 0,
    AI_PERM_RUN_COMMANDS    = 1 << 1,

    // File Read (Bit 8-9)
    AI_PERM_READ_SHIFT      = 8,
    AI_PERM_READ_MASK       = 0x3 << AI_PERM_READ_SHIFT,
    AI_PERM_READ_NO         = 0x0 << AI_PERM_READ_SHIFT,
    AI_PERM_READ_ASK        = 0x1 << AI_PERM_READ_SHIFT,
    AI_PERM_READ_ALWAYS     = 0x2 << AI_PERM_READ_SHIFT,

    // File Write (Bit 10-11)
    AI_PERM_WRITE_SHIFT     = 10,
    AI_PERM_WRITE_MASK      = 0x3 << AI_PERM_WRITE_SHIFT,
    AI_PERM_WRITE_NO        = 0x0 << AI_PERM_WRITE_SHIFT,
    AI_PERM_WRITE_ASK       = 0x1 << AI_PERM_WRITE_SHIFT,
    AI_PERM_WRITE_ALWAYS    = 0x2 << AI_PERM_WRITE_SHIFT,

    AI_PERM_SYSTEM_DEFAULT  = 0xFFFFFFFF
};

struct AISettings {
    BString engine; // "local" or "remote"
    BString plugin; // "Ollama", "OpenAI", ecc.
    BString model; // "llama3", "gpt-4o", ecc.
    BString api_key; // for remote engines
    BString base_url; // optional base url (for local plugins or custom endpoints)
    bool    base_url_override; // allow editing base_url even for remote plugins when true
    bool    use_remote_context;
    uint32  mcp_permissions;
};

// Load settings from ~/config/settings/AIService_settings (BMessage flattened)
// Returns true on success (settings populated), false on failure (defaults left)
bool LoadAISettings(AISettings& out);

// Save settings to ~/config/settings/AIService_settings
bool SaveAISettings(const AISettings& settings);

bool GetPluginAPIKey(const char* pluginName, BString& outKey);

// Convert settings to a minimal JSON string suitable for plugin init
// BString AISettingsToJSON(const AISettings& settings);

// Chiede ad ai_server la lista dei plugin installati.
// engineType può essere "local", "remote" o "all".
// Il BMessage restituito conterrà la struttura dei plugin.
status_t AIGetAvailablePlugins(const char* engineType, BMessage& outPlugins);

// Chiede ad ai_server i modelli disponibili per un determinato plugin.
// Restituisce la stringa JSON dei modelli (es. ["gpt-4", "gpt-3.5"])
status_t AIGetPluginModels(const char* pluginName, BString& outJsonModels);

// KeyStore helpers
// Returns true if a key exists for the given engine
bool HasAPIKey(const char* plugin);
// Removes the API key for engine (returns true on success)
bool RemoveAPIKey(const char* engine);
// Rotate (replace) the API key for engine with newKey (returns true on success)
//bool RotateAPIKey(const char* engine, const char* newKey);

#endif // SRC_KITS_AI_AICONFIG_H
