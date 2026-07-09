#ifndef SRC_KITS_AI_AICONFIG_H
#define SRC_KITS_AI_AICONFIG_H

#include <String.h>
#include <Message.h>
#include <Messenger.h>

struct AISettings {
    BString engine; // "local" or "remote"
    BString plugin; // "Ollama", "OpenAI", ecc.
    BString model; // "llama3", "gpt-4o", ecc.
    BString api_key; // for remote engines
    bool    use_remote_context;
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
bool RotateAPIKey(const char* engine, const char* newKey);

#endif // SRC_KITS_AI_AICONFIG_H
