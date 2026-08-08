// AIPlugin.h
// Minimal C ABI for AI service plugins
// Plugins are shared objects that export the functions below with C linkage.

#ifndef _OS_AI_PLUGIN_H
#define _OS_AI_PLUGIN_H

#include <stddef.h>
#include <stdint.h>
#include <SupportDefs.h>
#include <Message.h>
#include <Messenger.h>
#include <stdlib.h>
#include <new>
#include <String.h>

enum capabilities {
    AI_CAP_STREAMING			= 1 << 0,
    AI_CAP_REMOTE_CONTEXT		= 1 << 1, // Il plugin supporta i thread/contesti remoti
    AI_CAP_IMAGE_GENERATION		= 1 << 2,
    AI_CAP_MCP					= 1 << 3
};

struct AIPluginHandle {
    AIPluginHandle() = default;
    virtual ~AIPluginHandle() = default;

    // Disabilita la copia per evitare 'slicing' o gestione errata della memoria
    AIPluginHandle(const AIPluginHandle&) = delete;
    AIPluginHandle& operator=(const AIPluginHandle&) = delete;
};

struct AsyncArgs {
    char* api_key;
    char* model;
    char* notify_path;
    char* base_url;
    BMessage* context_copy;
    BMessenger server_messenger;

    AsyncArgs()
        : api_key(nullptr),
          model(nullptr),
          notify_path(nullptr),
          base_url(nullptr),
          context_copy(nullptr),
          server_messenger() {}

    virtual ~AsyncArgs() { // Un distruttore virtuale è sempre una buona pratica
        if (api_key) free(api_key);
        if (model) free(model);
        if (notify_path) free(notify_path);
        if (base_url) free(base_url);
        delete context_copy;
    }
};

#ifdef __cplusplus
extern "C" {
#endif

// Opaque handle for plugin instance
typedef void* ai_plugin_t;

ai_plugin_t ai_plugin_init(void);

// Free plugin instance
void ai_plugin_free(ai_plugin_t handle);

uint32 ai_plugin_get_capabilities(void);

// Synchronous text generation. Returns 0 on success, non-zero on error.
// Caller provides a response buffer; plugin must write a NUL-terminated string
// not exceeding response_len bytes (including NUL).
// contextMsg is mutable: the plugin may write back updated fields (e.g. remote_id).
status_t ai_plugin_generate_text_sync(ai_plugin_t handle,
                                      const char* prompt,
                                      char* response_buf,
                                      size_t response_len,
                                      BMessage* contextMsg);

// Asynchronous generation: plugin should queue work and return immediately.
// When ready, plugin MUST invoke a callback mechanism provided by the server
// (not defined here). For simplicity plugins can ignore this and return error.
//status_t ai_plugin_generate_text_async(ai_plugin_t handle,  const char* prompt, BMessage* contextMsg);
status_t ai_plugin_generate_async(ai_plugin_t handle, 
                                  const char* prompt, 
                                  BMessage* contextMsg);
// List available models; plugin writes a JSON array string into buffer similarly
// to generate_text_sync. Returns 0 on success.
//status_t ai_plugin_list_models(const char* config_json, char* out_buf, size_t out_len);
status_t ai_plugin_list_models(const BMessage* settingsMsg, char* out_buf, size_t out_len);

// Set active model by name.
// status_t ai_plugin_set_model(ai_plugin_t handle, const char* model_name);

typedef const char* (*plugin_get_name_t)(void);

void extract_json_field(const char* json, const char* key, char* out, size_t out_len);

#ifdef __cplusplus
}
#endif

#endif // _OS_AI_PLUGIN_H
